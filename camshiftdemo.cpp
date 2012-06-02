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

int vmin = 1;
int vmax = 256;
int smin = 1;

//plik video do wczytania
string filename = "black_bg_easy.avi";
int screenWidth, screenHeight;

void movemouse(RotatedRect trackBox, int wight, int height) {
	Point2f centr = trackBox.center;
	//normalizacja 100% video to 100% ekranu
	int iks = (int) centr.x * (screenWidth / wight);
	int igrek = (int) centr.y * (screenHeight / height);
	//ustawiamy kursor (btw 0,0 to lewy gorny rog ekranu)
	SetCursorPos(iks, igrek);
	//cout<<iks<<","<<igrek<<endl;
}

//Klik myszki 1- prawy 0-lewy
void mouseClick(bool side)
{
	INPUT    Input={0};

	Input.type= INPUT_MOUSE;

	if(side)
		{Input.mi.dwFlags  = MOUSEEVENTF_RIGHTDOWN;}
	else Input.mi.dwFlags  = MOUSEEVENTF_LEFTDOWN;
	SendInput( 1, &Input, sizeof(INPUT) );

	ZeroMemory(&Input,sizeof(INPUT));
	Input.type= INPUT_MOUSE;
	if(side)
		{Input.mi.dwFlags  = MOUSEEVENTF_RIGHTUP;}
	else Input.mi.dwFlags  = MOUSEEVENTF_LEFTUP;
	SendInput( 1, &Input, sizeof(INPUT) );
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

void help() {
	cout << "\n\nHot keys: \n"
			"\t'ESC' - quit the program\n"
			"\t'c' - stop the tracking\n"
			"\t'b' - switch to/from backprojection view\n"
			"\t'h' - show/hide object histogram\n"
			"\t' ' - pause video\n"
			"To initialize tracking, select the object with mouse\n";
}

const char* keys = { "{1|  | 0 | camera number}" };

int camshiftDemo(int argc, const char** argv) {
	//konstruktor VideoCapture, jako argument nazwa pliku wideo
	VideoCapture cap(filename);
	//pobiera parametry filmu
	int movie_width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
	int movie_height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);

	cout<<"movie_width: "<<movie_width<<", movie_height: "<<movie_height<<endl;

	bool stopE=0;
	Rect trackWindow;
	RotatedRect trackBox;
	int hsize = 16;
	float hranges[] = { 0, 180 };
	const float* phranges = hranges;

	namedWindow("Histogram", 0);
	namedWindow("CamShift Demo", 0);
	setMouseCallback("CamShift Demo", onMouse, 0);
	createTrackbar("Vmin", "CamShift Demo", &vmin, 256, 0);
	createTrackbar("Vmax", "CamShift Demo", &vmax, 256, 0);
	createTrackbar("Smin", "CamShift Demo", &smin, 256, 0);

	Mat frame, hsv, hue, mask, hist, histimg = Mat::zeros(200, 320, CV_8UC3), backproj;
	bool paused = false;

	Mat median_ia, binary_ia;

	for (;;) {
		if (!paused) {
			cap >> frame;
			if (frame.empty())
				break;
		}

		frame.copyTo(image);

		if (!paused) {
			//color space convertion:bgr->hsv
			cvtColor(image, hsv, CV_BGR2HSV);

			if (trackObject) {
				int _vmin = vmin, _vmax = vmax;

				//if element is between two arrays range
				inRange(hsv, Scalar(0, smin, MIN(_vmin,_vmax)), Scalar(180, 256, MAX(_vmin, _vmax)),
						mask);
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

					histimg = Scalar::all(0);
					int binW = histimg.cols / hsize;
					Mat buf(1, hsize, CV_8UC3);
					for (int i = 0; i < hsize; i++)
						buf.at<Vec3b>(i) = Vec3b(saturate_cast<uchar>(i * 180. / hsize), 255, 255);
					cvtColor(buf, buf, CV_HSV2BGR);

					//rysowanie histogramu
					for (int i = 0; i < hsize; i++) {
						int val = saturate_cast<int>(hist.at<float>(i) * histimg.rows / 255);
						//paint filled rectangle
						rectangle(histimg, Point(i * binW, histimg.rows),
								Point((i + 1) * binW, histimg.rows - val), Scalar(buf.at<Vec3b>(i)), -1,
								8);
					}
				}

				//if color match histogram of ROI -> wsteczna propagacja histogramu
				calcBackProject(&hue, 1, 0, hist, backproj, &phranges);
				backproj &= mask;

				//Finds an object center, size, and orientation.
				RotatedRect trackBox = CamShift(backproj, trackWindow,
						TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 10, 1));
				if(trackWindow.x==0){stopE=1;}
				else {stopE=0;}
				if (trackWindow.area() <= 1) {
					int cols = backproj.cols, rows = backproj.rows, r = (MIN(cols, rows) + 5) / 6;
					trackWindow = Rect(trackWindow.x - r, trackWindow.y - r, trackWindow.x + r,
							trackWindow.y + r) & Rect(0, 0, cols, rows);
				}

				//jeli widok prawdopodobieństwa
				if (backprojMode) {
					//grey scale of matching to histogram
					cvtColor(backproj, image, CV_GRAY2BGR);
				}

				if(!stopE){
					//paint ellipse around the object
					ellipse(image, trackBox, Scalar(0, 0, 255), 3, CV_AA);

					//rysuje prostokąt otaczajacy wykryty obszar
					rectangle(image, trackBox.boundingRect(), Scalar(0, 255, 255), 3, CV_AA);
				}

				//ruch kursorem myszy
				//movemouse(trackBox, movie_width, movie_height);
			}
		} else if (trackObject < 0)
			paused = false;

		if (selectObject && selection.width > 0 && selection.height > 0) {
			Mat roi(image, selection);
			bitwise_not(roi, roi);
		}

		imshow("CamShift Demo", image);
		imshow("Histogram", histimg);

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
		case 'h':
			showHist = !showHist;
			if (!showHist)
				destroyWindow("Histogram");
			else
				namedWindow("Histogram", 1);
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

int main(int argc, const char** argv) {
	help();

	screenWidth = GetSystemMetrics(SM_CXSCREEN);
	screenHeight = GetSystemMetrics(SM_CYSCREEN);
	cout << screenWidth << "," << screenHeight << " - resolution of the screen [px]" << endl;

	camshiftDemo(argc, argv);

	cout << clock() << " - number of clock ticks elapsed since the program was launched" << endl;

	/* test kliniecie prawym przyciskiem myszy
	 *SetCursorPos(300, 300);
	mouseClick(1);*/

	return 0;
}

