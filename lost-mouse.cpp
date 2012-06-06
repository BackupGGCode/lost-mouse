#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

#include <iostream>
#include <windows.h>
#include <ctype.h>

//do testów
#include <time.h>
#include <iomanip>

using namespace cv;
using namespace std;

/* 1 - wszystko wyłaczone - OFF
 * 0 - wszystko właczone - ON
 * mod2 = 1 - położenie sledzonego obiektu, wielkosc, rotacja
 * mod3 = 1 - ilosc wykrytych cech do klikniecia, jesli ono zaszło
 * mod5 = 1 - ilosc wykrytych cech do klinkniecia, nawet jesli nie zaszlo
 * mod7 = 1 spowolnione odtwarzenie video
 */
int debug = 3 * 7;

//pokazywanie prawdopodobienstwa wstecznej propagacji histogramu
bool backprojMode = false;
//stan do rysowania zaznaczanego obszaru
bool selectObject = false;
bool showHist = true;
//stan sledzonego obiektu
int trackObject = 0;
Mat image;
Point origin;
Rect selection;
//0-select myszka, 1-select automatycznie ze srodka filmu
bool select_mouse_autom = 0;

int screenWidth, screenHeight;

void movemouse(RotatedRect trackBox, int wight, int height) {
	Point2f centr = trackBox.center;
	//normalizacja 100% video to 100% ekranu
	/*Size2f size=trackBox.size;
	 cout<<"wysokosc "<<size.height<<", szerokosc"<<size.width<<endl;
	 cout<<size.height/size.width<<endl;*/
	int iks = (int) centr.x * (screenWidth / wight);
	int igrek = (int) centr.y * (screenHeight / height);
	//ustawiamy kursor (btw 0,0 to lewy gorny rog ekranu)
	SetCursorPos(iks, igrek);
	//cout<<iks<<","<<igrek<<endl;
}

//Klik myszki 1- prawy 0-lewy
void mouseClick(bool side) {
	INPUT Input = { 0 };

	Input.type = INPUT_MOUSE;

	if (side) {
		Input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
	} else
		Input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	SendInput(1, &Input, sizeof(INPUT));

	ZeroMemory(&Input,sizeof(INPUT));
	Input.type = INPUT_MOUSE;
	if (side) {
		Input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
	} else
		Input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	SendInput(1, &Input, sizeof(INPUT));
}

void onMouse(int event, int x, int y, int, void*) {
	if (selectObject) {
		selection.x = MIN(x, origin.x);
		selection.y = MIN(y, origin.y);
		selection.width = std::abs(x - origin.x);
		selection.height = std::abs(y - origin.y);
		selection &= Rect(0, 0, image.cols, image.rows);
	}

	switch (event) {
	case CV_EVENT_LBUTTONDOWN:
		origin = Point(x, y);
		selection = Rect(x, y, 0, 0);
		selectObject = true;
		break;
	case CV_EVENT_LBUTTONUP:
		selectObject = false;
		if (selection.width > 0 && selection.height > 0)
			trackObject = -1;
		break;
	}
}

//funkcja normalizująca kąt
//zapobiega przejsciu 360 -> 0 stopni
float inline normalizeAngle(float angle) {
	return angle < 90.0f ? angle + 180.0f : angle;
}

int lost_mouse(VideoCapture& cap) {
	cout << endl << "w funkcji lost_mouse(VideoCapture&)" << endl;

	//obszar do automatycznego zaznaczenia dłoni
	Rect selection_autom;
	Rect trackWindow;
	//znalezioy wynik camshift'a
	RotatedRect trackBox;
	//przechowywuja klatke
	Mat hsv_ia, hue_ia, luminancy_ia, backproj;
	//do masek i do ROI
	Mat mask, roi, maskroi;
	//do obsługi histogramu
	Mat hist, histimg = Mat::zeros(200, 320, CV_8UC3);
	int hsize = 16;
	float hranges[] = { 0, 180 };
	const float* phranges = hranges;
	//do mix channels z hsv -> h
	int mix_ch[] = { 0, 0 };
	//min i max wartosci hsv do maski (na podstawie której okreslany jest ROI)
	Scalar hsv_min(0, 30, MIN(10,256)), hsv_max(180, 256, MAX(10, 256));

	//obecny stan maszyny stanowej
	int stan;

	bool paused = false;

	//video jest brane z kamery video (opcja jest dostępna tylko dla kamer wideo)
	bool camera_video = cap.get(CV_CAP_PROP_FPS) == 0;
	cout << camera_video << " - obraz z kamery wideo" << endl;

	/* numer aktualnego gestu
	 * 0 - brak gestu
	 * 1 - LPM
	 * 2 - PPM
	 * ujemna - blad
	 */
	int gesture = 0;
	int gesture_timeout = 0;

	//historia powierzchni wykrytego obszaru
	float area4 = 0, area3 = 0, area2 = 0, area1 = 0, area = 0;
	//historia stosunku wysokosci do szerokosci obszaru
	float rot4 = 0, rot3 = 0, rot2 = 0, rot1 = 0, rot = 0;
	//historia współrzędnej y polozenia srodka obszaru
	float pozY4 = 0, pozY3 = 0, pozY2 = 0, pozY1 = 0, pozY = 0;
	//historia współrzędnej x polozenia srodka obszaru
	float pozX4 = 0, pozX3 = 0, pozX2 = 0, pozX1 = 0, pozX = 0;
	//obliczenia dla obecnej dlatki , like  %D - zmiany w ostatnich klatach
	float areaD, rotDiff, rotDL, rotDR, pozYD, prop;

	//pobiera parametry klatki video
	int movie_width = cap.get(CV_CAP_PROP_FRAME_WIDTH), movie_height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
	//naprawiony fps
	int select_mouse_autom_timeout = (camera_video ? 30 : cap.get(CV_CAP_PROP_FPS)) * 3.5;

	cout << movie_width << "," << movie_height << " - wymiary obrazu szerokosc,wysokosc [px]" << endl;
	cout << cap.get(CV_CAP_PROP_FPS) << " - liczba klatek na sekunde (przy kamerze = 30fps)" << endl;

	//przygotowanie okna i wykrywania/zaznaczania dłoni
	namedWindow("lost-mouse podglad");
	if (select_mouse_autom) {
		//wyznaczenie obszaru w ktorym ma sie znalesc reka
		selection_autom.x = movie_width * 0.45;
		selection_autom.y = movie_height * 0.45;
		selection_autom.width = movie_width * 0.1;
		selection_autom.height = movie_height * 0.1;
	} else {
		//podpiecie funkcji do zaznaczania obszaru
		setMouseCallback("lost-mouse podglad", onMouse, 0);
	}

	for (int frame_counter = 0;; frame_counter++) {
		if (paused) {
			frame_counter--;
		} else {
			cap >> image;
			if (image.empty())
				break;
		}

		//pobranie koloru dla dloni po odpowiednim czasie
		if (select_mouse_autom && frame_counter == select_mouse_autom_timeout) {
			selection = selection_autom;
			trackObject = -1;
			cout << "automatyczne pobieranie koloru dloni - zrobione!" << endl;
		}

		if (!paused) {
			//zmiana przestrzeni barwnej:bgr->hsv
			cvtColor(image, hsv_ia, CV_BGR2HSV);

			if (trackObject) {
				//stwierdzenie czy dany element tablicy ma wartosc pomiedzy min i max
				inRange(hsv_ia, hsv_min, hsv_max, mask);

				//skopiowanie luminancji (Hue) z przestrzeni barw HSV
				hue_ia.create(hsv_ia.size(), hsv_ia.depth());
				mixChannels(&hsv_ia, 1, &hue_ia, 1, mix_ch, 1);

				//poprawienie jakosci obrazu
				{
					//filtr medianowy - popraiwa szumy
					luminancy_ia.create(hue_ia.size(), hue_ia.depth());
					medianBlur(hue_ia, luminancy_ia, 3);
				}

				//obliczanie histogramu
				if (trackObject < 0) {
					//tworzenie maski dla ROI
					roi = Mat(luminancy_ia, selection);
					maskroi = Mat(mask, selection);

					//obliczanie histogramu
					calcHist(&roi, 1, 0, maskroi, hist, 1, &hsize, &phranges);

					//normalizacja wartosci histogramu
					normalize(hist, hist, 0, 255, CV_MINMAX);

					trackWindow = selection;
					trackObject = 1;
				}

				//wsteczna propagacja histogramu -> czy kolor pasuje zaznaczonemu obszarowi
				calcBackProject(&luminancy_ia, 1, 0, hist, backproj, &phranges);
				backproj &= mask;

				//znajduje srodek, wymiary i orientacje obiektu
				trackBox = CamShift(backproj, trackWindow,
						TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 10, 1));

				if (trackWindow.area() <= 1) {
					int cols = backproj.cols, rows = backproj.rows, r = (MIN(cols, rows) + 5) / 6;
					trackWindow = Rect(trackWindow.x - r, trackWindow.y - r, trackWindow.x + r,
							trackWindow.y + r) & Rect(0, 0, cols, rows);
				}

				//ruch kursorem myszy
				//movemouse(trackBox, movie_width, movie_height);

				//wykrywanie kliknięć
				{
					//przepisanie historii pozycji i wymiarów
					area4 = area3;
					area3 = area2;
					area2 = area1;
					area1 = area;
					rot4 = rot3;
					rot3 = rot2;
					rot2 = rot1;
					rot1 = rot;
					pozX4 = pozX3;
					pozX3 = pozX2;
					pozX2 = pozX1;
					pozX1 = pozX;
					pozY4 = pozY3;
					pozY3 = pozY2;
					pozY2 = pozY1;
					pozY1 = pozY;

					//przypisanie aktualnego stanu
					area = trackBox.size.height * trackBox.size.width;
					rot = normalizeAngle(trackBox.angle);
					pozY = trackBox.center.y;
					pozX = trackBox.center.x;
					prop = trackBox.size.height / trackBox.size.width;

					//maszyna stanów
					Scalar color2 = Scalar(0, 0, 0);

					//rozmiar ramki w % od brzegu okna
					float border_check = 0.1;
					//sprawdzanie, czy reka w polu
					if(pozX<movie_width * border_check || pozX>movie_width * (1-2*border_check) || pozY<movie_height * border_check || pozY>movie_height * (1-2*border_check)){
						//obecnie poza polem
						if(pozX1<movie_width * border_check || pozX1>movie_width * (1-2*border_check) || pozY1<movie_height * border_check || pozY1>movie_height * (1-2*border_check)){
							//obecnie i wczesniej poza polem => reka poza polem
							stan = 0;
						}else{
							//obecnie poza polem, a wczesniej w polu => reka wyszla z pola widzenia
							stan = 3;
						}
					}else{
						//obecnie w polu widzenia kamery
						if(pozX1<movie_width * border_check || pozX1>movie_width * (1-2*border_check) || pozY1<movie_height * border_check || pozY1>movie_height * (1-2*border_check)){
							//obecnie w polu widzenia, wczesniej poza polem => reka sie pojawia
							stan = 1;
						}else{
							//obecnie w polu i wczesniej tez w polu => reka w polu
							stan = 2;
						}
					}


					if(stan == 0){ //reka znajduje sie poza polem widzenia kamery

						//put some code here
						color2 = Scalar(0, 0, 255);
					}else if(stan == 1){ //reka wchodzi w pole widzenia kamery

						//put some code here
						color2 = Scalar(0, 255, 255);
					}else if(stan == 2){ //reka znajduje sie w polu widzenia kamery

						//put some code here
						color2 = Scalar(0, 255, 0);
					}else if(stan == 3){ //reka wychodzi z pola widzenia kamery

						//put some code here
						color2 = Scalar(255, 255, 0);
					}

					//rysowanie ramki kontrolnej
					rectangle(image, Rect(movie_width * border_check, movie_height * border_check,movie_width * (1-2*border_check),movie_height * (1-2*border_check)), color2, 1, CV_AA);



					//obliczanie zmian na klatkę obecną
					rotDiff = rot - rot4;
					areaD = (area4 > area3) + (area3 > area2) + (area2 > area1) + (area1 > area);
					rotDR = (rot4 < rot3) + (rot3 < rot2) + (rot2 < rot1) + (rot1 < rot);
					rotDL = (rot4 > rot3) + (rot3 > rot2) + (rot2 > rot1) + (rot1 > rot);
					pozYD = (pozY4 < pozY3) + (pozY3 < pozY2) + (pozY2 < pozY1) + (pozY1 < pozY);

					if (debug % 5 == 0) {
						cout << setw(5) << frame_counter << "; warunki;" << setw(3) << areaD << ";"
								<< setw(3) << rotDR << ";" << setw(3) << rotDL << ";" << setw(9)
								<< rotDiff << ";" << setw(2) << pozYD << ";" << endl;
					}

					float angleMin = 15, angleMax = 60;

					//detekcja LPM
					if (((/*wysokosc*/1 < pozYD && pozY > pozY4)
							&& (/*powierzchania*/2 < areaD && area < area4))
							&& ((/*lewo*/2 < rotDL && -angleMax < rotDiff && rotDiff < -angleMin)
									|| (/*prawo*/2 < rotDR && angleMax > rotDiff && rotDiff > angleMin))) {
						gesture = 1;
						gesture_timeout = 5;

						//mouseClick(0);

						if (debug % 3 == 0 && (debug % 5 != 0 || debug == 0)) {
							cout << setw(5) << frame_counter << "; LPM;" << setw(3) << areaD << ";"
									<< setw(3) << rotDR << ";" << setw(3) << rotDL << ";" << setw(9)
									<< rotDiff << ";" << setw(2) << pozYD << ";" << endl;
						}

						//zerowanie historii - cooldown 5 klatek na gesty
						area4 = area3 = area2 = area1 = area = 0;
						rot4 = rot3 = rot2 = rot1 = rot = 0;
						pozY4 = pozY3 = pozY2 = pozY1 = pozY = 0;
						continue;
					}

					angleMax = 5;

					//detekcja PPM
					if ((/*proporcja*/1.9 < prop && prop < 3.5) && (/*kat*/160 < rot && rot < 200)
							&& (/*zmiana kata*/-angleMax < rotDiff && rotDiff < angleMax)
							&& (/*powierzchania*/2 < areaD && 0.7 < area / area4 && area / area4 < 0.9)) {
						gesture = 2;
						gesture_timeout = 5;

						//mouseClick(1);

						if (debug % 3 == 0 && (debug % 5 != 0 || debug == 0)) {
							cout << setw(5) << frame_counter << "; PPM;" << setw(3) << areaD << ";"
									<< setw(9) << rotDiff << ";" << setw(9) << area / area4 << ";"
									<< setw(9) << rot << ";" << setw(9) << prop << ";" << endl;
						}

						//zerowanie historii - cooldown 5 klatek na gesty
						area4 = area3 = area2 = area1 = area = 0;
						rot4 = rot3 = rot2 = rot1 = rot = 0;
						pozY4 = pozY3 = pozY2 = pozY1 = pozY = 0;
						continue;
					}
				}
			}
		} else if (trackObject < 0) {
			paused = false;
		}

		//widok prawdopodobienstwa jak bardzo dany kolor pasuje do zaznaczoneg obszaru
		if (backprojMode && backproj.rows != 0) {
			cvtColor(backproj, image, CV_GRAY2BGR);
		}

		//rysowanie wykrytego obszaru tylko jeli wykryło obszar x!=0 || y!0=0
		if (trackBox.center.x && trackBox.center.y) {
			Scalar color;
			switch (gesture) {
			case 0:
				color = Scalar(0, 255, 0);
				break;
			case 1:
				color = Scalar(255, 0, 0);
				break;
			case 2:
				color = Scalar(255, 0, 255);
				break;
			default:
				color = Scalar(0, 0, 255);
				break;
			}

			try {
				//rysuje prostokat otaczajacy wykryty obszar
				rectangle(image, trackBox.boundingRect(), color, 1, CV_AA);

				//rysuje prostokat dopasowany do obszaru
				Point2f vertices[4];
				trackBox.points(vertices);
				for (int i = 0; i < 4; i++)
					line(image, vertices[i], vertices[(i + 1) % 4], color, 2);

				//rysuje eklipse
				ellipse(image, trackBox, color, 3, CV_AA);

				//rysuje srodek znalezionego obszaru
				circle(image, trackBox.center, 4, color, -1);
			} catch (Exception& e) {
				cout << setw(5) << frame_counter << "; bład rysowania znaczników" << endl;
			}

			if (!paused && debug % 2 == 0) {
				cout << setw(5) << frame_counter << ";" << setw(6) << trackBox.center.x << ";" << setw(6)
						<< trackBox.center.y << " ; " << setw(9) << trackBox.size.width << ";" << setw(8)
						<< trackBox.size.height << ";" << setw(9) << normalizeAngle(trackBox.angle)
						<< ";" << endl;
			}

			if (!paused && !camera_video && debug % 7 == 0)
				Sleep(100);
		}

		//rysowanie prostokata od selekcji
		if (select_mouse_autom && frame_counter < select_mouse_autom_timeout) {
			rectangle(image, selection_autom, Scalar(255, 255, 0), 3, CV_AA);
		}

		imshow("lost-mouse podglad", image);

		char c = (char) waitKey(10);
		if (c == 27)
			break;
		switch (c) {
		case 'b':
			backprojMode = !backprojMode;
			break;
		case ',':
			cout << frame_counter << " znacznik LLP" << endl;
			break;
		case '.':
			cout << frame_counter << " znacznik PPM" << endl;
			break;
		case 'c':
			trackObject = 0;
			frame_counter = 0;
			backprojMode = false;
			trackBox = RotatedRect();
			histimg = Scalar::all(0);
			cout << "reset histogramu" << endl;
			break;
		case ' ':
			paused = !paused;
			break;
		default:
			break;
		}

		if (0 < gesture && --gesture_timeout == 0)
			gesture = 0;
	}

	return 0;
}

void help() {
	cout << "\nskroty klawiszowe: \n"
			"\t'ESC' - wyjscie z programu\n"
			"\t'c' - resetowanie zaznaczonego obszaru, zaprzestanie sledzenia\n"
			"\t'b' - podglad widoku wstecznej projekcji histogramu\n"
			"\t' ' - stopklatka\n";
}

void help_arg() {
	cout << "\nargumenty:\n"
			"arg1 - sciezka do pliku wideo, jesli 'null' to obraz jest z kamery, domyslnie:'null'\n"
			"arg2 - automatyczne zaznaczanie dloni: {true,false}, domyslnie:true\n";
}

int main(int argc, const char** argv) {
//mozliwe arguemnty programu
	string str_help("--help");
	if (argc == 2 && str_help.compare(argv[1]) == 0) {
		help_arg();
		return 0;
	}

	screenWidth = GetSystemMetrics(SM_CXSCREEN);
	screenHeight = GetSystemMetrics(SM_CYSCREEN);
	cout << screenWidth << "," << screenHeight << " - wymiary ekranu [piksele]" << endl;

//wczytanie streamu video
	VideoCapture vcap;
	string str_null("null");
	if (argc < 2 || str_null.compare(argv[1]) == 0) {
		//wczytywanie z kamery
		vcap.open(0);
		cout << "zrodlo obrazu: kamera wideo" << endl;
	} else {
		//wczytywanie z pliku
		string filename = (LPCTSTR) argv[1];
		vcap.open(filename);
		cout << "zrodlo obrazu: " << argv[1] << endl;
	}
	if (!vcap.isOpened()) {
		//obsluga blednego pliku video
		cout << "blad - nie mozna odczytac obrazu z podanego zrodla\n";
		return -1;
	}

//automatyczne zaznaczanie dłoni w srodku poczatkowej klatki video
	string str_select("true");
	select_mouse_autom = argc < 3 || str_select.compare(argv[2]) == 0;
	cout << "automatyczne zaznaczanie dłoni: " << (select_mouse_autom ? "true" : "false") << endl;

	help();

	clock_t before = clock();
	lost_mouse(vcap);
	clock_t after = clock();

	cout << endl << after - before << " - czas sledzenia dloni [ms]" << endl;

//test kliniecie prawym przyciskiem myszy
//SetCursorPos(300, 300);
//mouseClick(1);

	return 0;
}
