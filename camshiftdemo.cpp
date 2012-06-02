#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

#include <iostream>
#include <ctype.h>
#include <windows.h>
#include <time.h>

using namespace cv;
using namespace std;

bool backprojMode = false;
bool selectObject = false;
bool showHist = true;
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

int camshiftDemo(VideoCapture& cap) {
	//pobiera parametry filmu
	int movie_width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
	int movie_height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
	int select_mouse_autom_timeout = (cap.get(CV_CAP_PROP_FPS) ? cap.get(CV_CAP_PROP_FPS) : 30) * 3.5;

	cout << movie_width << "," << movie_height << " - video width,height [px]" << endl;
	cout << cap.get(CV_CAP_PROP_FPS) << " - viedo fps (in camera 30fps assumed)" << endl;

	bool stopE = 0;
	Rect trackWindow;
	RotatedRect trackBox;
	int hsize = 16;
	float hranges[] = { 0, 180 };
	const float* phranges = hranges;

	namedWindow("lost-mouse preview");

	Rect selection_autom;
	if (select_mouse_autom) {
		selection_autom.x = movie_width *0.45;
		selection_autom.y = movie_height *0.4;
		selection_autom.width = movie_width *0.1;
		selection_autom.height = movie_height *0.2;
	} else{
		setMouseCallback("lost-mouse preview", onMouse, 0);
	}

	Mat frame, hsv, hue, mask, hist, median_ia, binary_ia, backproj;
	Mat histimg = Mat::zeros(200, 320, CV_8UC3);
	bool paused = false;


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
			cout << "automated hand selection - done!" << endl;
		}

		frame.copyTo(image);

		if (!paused) {
			//color space convertion:bgr->hsv
			cvtColor(image, hsv, CV_BGR2HSV);

			if (trackObject) {
				//TODO: wtf is vmin,vmax,smin
				int vmin = 10, vmax = 256, smin = 30;

				//if element is between two arrays range
				inRange(hsv, Scalar(0, smin, MIN(vmin,vmax)), Scalar(180, 256, MAX(vmin, vmax)), mask);
				int ch[] = { 0, 0 };

				//creating blank image
				hue.create(hsv.size(), hsv.depth());

				//copy hue from HSV color space
				mixChannels(&hsv, 1, &hue, 1, ch, 1);

				//binaryzacja
				binary_ia.create(hue.size(), hue.depth());
				threshold(hue, binary_ia, 32.0, 256.0, THRESH_BINARY);

				//median filter
				median_ia.create(hue.size(), hue.depth());
				medianBlur(binary_ia, median_ia, 3);

				hue = median_ia;

				if (trackObject < 0) {
					//creating RegionOfInterest and Mask of it
					Mat roi(hue, selection), maskroi(mask, selection);

					//calculating Histogram
					calcHist(&roi, 1, 0, maskroi, hist, 1, &hsize, &phranges);

					//normalization of values for hitogram
					normalize(hist, hist, 0, 255, CV_MINMAX);

					trackWindow = selection;
					trackObject = 1;
				}

				//if color match histogram of ROI -> wsteczna propagacja histogramu
				calcBackProject(&hue, 1, 0, hist, backproj, &phranges);
				backproj &= mask;

				//Finds an object center, size, and orientation.
				RotatedRect trackBox = CamShift(backproj, trackWindow,
						TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 10, 1));
				if (trackWindow.x == 0) {
					stopE = 1;
				} else {
					stopE = 0;
				}
				if (trackWindow.area() <= 1) {
					int cols = backproj.cols, rows = backproj.rows, r = (MIN(cols, rows) + 5) / 6;
					trackWindow = Rect(trackWindow.x - r, trackWindow.y - r, trackWindow.x + r,
							trackWindow.y + r) & Rect(0, 0, cols, rows);
				}

				//jeli widok prawdopodobienstwa
				if (backprojMode) {
					//grey scale of matching to histogram
					cvtColor(backproj, image, CV_GRAY2BGR);
				}

				if (!stopE) {
					//paint ellipse around the object
					ellipse(image, trackBox, Scalar(0, 0, 255), 3, CV_AA);

					//rysuje prostokat otaczajacy wykryty obszar
					rectangle(image, trackBox.boundingRect(), Scalar(0, 255, 255), 3, CV_AA);

					Point2f vertices[4];
					trackBox.points(vertices);
					for (int i = 0; i < 4; i++)
						line(image, vertices[i], vertices[(i + 1) % 4], Scalar(0, 255, 0), 2);
				}

				//ruch kursorem myszy
				//movemouse(trackBox, movie_width, movie_height);
			}
		} else if (trackObject < 0){
			paused = false;
		}

		if (selectObject && selection.width > 0 && selection.height > 0) {
			Mat roi(image, selection);
			bitwise_not(roi, roi);
		}

		//rysowanie prostokata od selekcji
		if (select_mouse_autom && frame_counter < select_mouse_autom_timeout) {
			rectangle(image, selection_autom, Scalar(255, 255, 0), 3, CV_AA);
		}

		imshow("lost-mouse preview", image);

		char c = (char) waitKey(10);
		if (c == 27)
			break;
		switch (c) {
		case 'b':
			backprojMode = !backprojMode;
			break;
		case 'c':
			trackObject = 0;
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
	cout << "\nHot keys: \n"
			"\t'ESC' - quit the program\n"
			"\t'c' - stop the tracking\n"
			"\t'b' - switch to/from backprojection view\n"
			"\t' ' - pause video\n"
			"To initialize tracking, select the object with mouse\n\n";
}

void help_arg() {
	cout << "\narguments:\n"
			"arg1 - video file path, or 'null' if capturing from camera, default:null\n"
			"arg2 - automatic hand selection: {true,false}, default:true\n";
}

int main(int argc, const char** argv) {

	string str_help("--help");
	if (argc == 2 && str_help.compare(argv[1]) == 0) {
		help_arg();
		return 0;
	}

	screenWidth = GetSystemMetrics(SM_CXSCREEN);
	screenHeight = GetSystemMetrics(SM_CYSCREEN);
	cout << screenWidth << "," << screenHeight << " - resolution of the screen [px]" << endl;

	VideoCapture vcap;
	string str_null("null");
	if (argc < 2 || str_null.compare(argv[1]) == 0) {
		//wczytywanie z kamery
		vcap.open(0);
		if (!vcap.isOpened()) {
			help();
			cout << "error - could not initialize capturing from camera\n";
			return -1;
		}
		cout << "capture from: camera video" << endl;
	} else {
		//wczytywanie z pliku
		string filename = (LPCTSTR) argv[1];
		vcap = VideoCapture(filename);
		cout << "capture from file: " << argv[1] << endl;
	}

	string str_select("true");
	//automatyczne zaznaczanie dłoni w srodku poczatkowej klatki video
	select_mouse_autom = argc < 3 || str_select.compare(argv[2]) == 0;
	cout << "automatic hand selection: " << (select_mouse_autom ? "true" : "false") << endl;

	help();

	clock_t before = clock();
	camshiftDemo(vcap);
	clock_t after = clock();

	cout << endl << after - before << " - number of clock ticks elapsed during hand function" << endl;

	//test kliniecie prawym przyciskiem myszy
	//SetCursorPos(300, 300);
	//mouseClick(1);

	return 0;
}
