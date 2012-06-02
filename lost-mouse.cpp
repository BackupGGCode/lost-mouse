#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

#include <iostream>
#include <windows.h>
#include <ctype.h>
#include <time.h>

using namespace cv;
using namespace std;

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

int lost_mouse(VideoCapture& cap) {
	//obszar do automatycznego zaznaczenia dłoni
	Rect selection_autom;
	Rect trackWindow;
	//znalezioy wynik camshift'a
	RotatedRect trackBox;
	//przechowywuja klatke
	Mat frame, hsv_ia, hue_ia, median_ia, binary_ia, luminancy_ia, mask, backproj;
	//do obsługi histogramu
	Mat hist, histimg = Mat::zeros(200, 320, CV_8UC3);
	int hsize = 16;
	float hranges[] = { 0, 180 };
	const float* phranges = hranges;
	//min i max wartosci hsv do maski (na podstawie której okreslany jest ROI)
	Scalar hsv_min(0, 30, MIN(10,256)), hsv_max(180, 256, MAX(10, 256));

	bool paused = false;

	namedWindow("lost-mouse podglad");

	//pobiera parametry klatki video
	int movie_width = cap.get(CV_CAP_PROP_FRAME_WIDTH), movie_height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
	//naprawiony fps
	int select_mouse_autom_timeout = (cap.get(CV_CAP_PROP_FPS) ? cap.get(CV_CAP_PROP_FPS) : 30) * 3.5;

	cout << movie_width << "," << movie_height << " - wymiary obrazu szerokosc,wysokosc [px]" << endl;
	cout << cap.get(CV_CAP_PROP_FPS) << " - liczba klatek na sekunde (przy kamerze = 30fps)" << endl;

	if (select_mouse_autom) {
		selection_autom.x = movie_width * 0.45;
		selection_autom.y = movie_height * 0.4;
		selection_autom.width = movie_width * 0.1;
		selection_autom.height = movie_height * 0.2;
	} else {
		setMouseCallback("lost-mouse podglad", onMouse, 0);
	}

	for (int frame_counter = 0;; frame_counter++) {
		if (paused) {
			frame_counter--;
		} else {
			cap >> frame;
			if (frame.empty())
				break;
		}
		if (select_mouse_autom && frame_counter == select_mouse_autom_timeout) {
			selection = selection_autom;
			trackObject = -1;
			cout << "automatyczne pobieranie koloru dloni - zrobione!" << endl;
		}

		frame.copyTo(image);

		if (!paused) {
			//zmiana przestrzeni barwnej:bgr->hsv
			cvtColor(image, hsv_ia, CV_BGR2HSV);

			if (trackObject) {
				//stwierdzenie czy dany element tablicy ma wartosc pomiedzy min i max
				inRange(hsv_ia, hsv_min, hsv_max, mask);
				int ch[] = { 0, 0 };

				//skopiowanie luminancji (Hue) z przestrzeni barw HSV
				hue_ia.create(hsv_ia.size(), hsv_ia.depth());
				mixChannels(&hsv_ia, 1, &hue_ia, 1, ch, 1);

				//binaryzacja
				binary_ia.create(hue_ia.size(), hue_ia.depth());
				threshold(hue_ia, binary_ia, 32.0, 256.0, THRESH_BINARY);

				//filtr medianowy
				median_ia.create(binary_ia.size(), binary_ia.depth());
				medianBlur(binary_ia, median_ia, 3);

				luminancy_ia = median_ia;

				if (trackObject < 0) {
					//tworzenie maski dla ROI
					Mat roi(luminancy_ia, selection), maskroi(mask, selection);

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
			}
		} else if (trackObject < 0) {
			paused = false;
		}

		//widok prawdopodobienstwa jak bardzo dany kolor pasuje do zaznaczoneg obszaru
		if (paused && backprojMode && backproj.rows != 0) {
			cvtColor(backproj, image, CV_GRAY2BGR);
		}

		if (selectObject && selection.width > 0 && selection.height > 0) {
			Mat roi(image, selection);
			bitwise_not(roi, roi);
		}

		//rysowanie wykrytego obszaru tylko jeli wykryło obszar x!=0 || y!0=0
		if (trackWindow.x || trackWindow.y) {
			//rysuje prostokat otaczajacy wykryty obszar
			rectangle(image, trackBox.boundingRect(), Scalar(0, 255, 255), 1, CV_AA);

			//rysuje prostokat dopasowany do obszaru
			Point2f vertices[4];
			trackBox.points(vertices);
			for (int i = 0; i < 4; i++)
				line(image, vertices[i], vertices[(i + 1) % 4], Scalar(0, 255, 0), 2);

			//rysuje eklipse
			ellipse(image, trackBox, Scalar(0, 0, 255), 2, CV_AA);

			//rysuje srodek znalezionego obszaru
			circle(image, trackBox.center, 2, Scalar(0, 0, 255), 3);
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
		case 'c':
			trackObject = 0;
			frame_counter = 0;
			histimg = Scalar::all(0);
			break;
		case ' ':
			paused = !paused;
			break;
		default:
			break;
		}
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
	cout
			<< "\nargumenty:\n"
					"arg1 - sciezka do pliku wideo, jesli 'null' to brany jest obraz z kamery, domyslnie:'null'\n"
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
