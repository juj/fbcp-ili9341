

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <regex>

#include "VG/openvg.h"
#include "VG/vgu.h"
#include "fontinfo.h"
#include "shapes.h"

#include <linux/input.h>
#include <fcntl.h>
#include <pthread.h>

#include "calibrate.h"

// Target on-screen points
POINT pointTargets[4] = {
    {15, 15},
    {-15, 15},
    {-15, -15},
    {15, -15}
};

// Recorded touch points
POINT pointTouches[4];

MATRIX calibMatrix;

#define xHairLines_ROWS 8
int xHairLines[8][4] = {
    10, 15, 20, 15, // lower left
    15, 10, 15, 20,
    -20, 15, -10, 15, // lower right
    -15, 10, -15, 20,
    10, -15, 20, -15, // upper left
    15, -10, 15, -20,
    -20, -15, -10, -15, // upper right
    -15, -10, -15, -20
};

// touch state structure
typedef struct {
    int fd;
    char* evbuff[80];
    VGfloat x, y, z;
    int max_x, max_y;
} touch_t;

touch_t touch;            // global touch state
int left_count = 0;
int quitState = 0;
#define    CUR_SIZ  16                       // cursor size, pixels beyond centre dot

// evenThread reads from the touch input file
void *eventThread(void *arg) {
#define OPENDEVICE_TOUCH    if ((touch.fd = open("/tmp/TCfifo", O_RDONLY)) < 0) { \
        fprintf(stderr, "Error opening touch!\n"); \
        quitState = 1; \
        return &quitState;}

    // Open touch driver
    OPENDEVICE_TOUCH
    touch.x = touch.max_x / 2;               //Reset touch
    touch.y = touch.max_y / 2;
    
    while (1) {
        uint16_t words16read[4];
        usleep(500);
        if(read(touch.fd, words16read, sizeof(uint16_t) * 4 )) {
            touch.x = words16read[0];
            touch.y = words16read[1];
            touch.z = words16read[2];
            close(touch.fd);
            OPENDEVICE_TOUCH
        }
    }
}

static int cur_sx, cur_sy, cur_w, cur_h;    // cursor location and dimensions
static int cur_saved = 0;    // amount of data saved in cursor image backup

// saveCursor saves the pixels under the touch cursor
void saveCursor(VGImage CursorBuffer, int curx, int cury, int screen_width, int screen_height, int s) {
    int sx, sy, ex, ey;
    
    sx = curx - s;                       // horizontal
    if (sx < 0) {
        sx = 0;
    }
    ex = curx + s;
    if (ex > screen_width) {
        ex = screen_width;
    }
    cur_sx = sx;
    cur_w = ex - sx;
    
    sy = cury - s;                       // vertical
    if (sy < 0) {
        sy = 0;
    }
    ey = cury + s;
    if (ey > screen_height) {
        ey = screen_height;
    }
    cur_sy = sy;
    cur_h = ey - sy;
    
    vgGetPixels(CursorBuffer, 0, 0, cur_sx, cur_sy, cur_w, cur_h);
    cur_saved = cur_w * cur_h;
}

// restoreCursor restores the pixels under the touch cursor
void restoreCursor(VGImage CursorBuffer) {
    if (cur_saved != 0) {
        vgSetPixels(cur_sx, cur_sy, CursorBuffer, 0, 0, cur_w, cur_h);
    }
}

// circleCursor draws a translucent circle as the touch cursor
void circleCursor(int curx, int cury, int width, int height, int s) {
    Fill(100, 0, 0, 0.50);
    Circle(curx, cury, s);
    Fill(0, 0, 0, 1);
    Circle(curx, cury, 2);
}

// touchinit starts the touch event thread
int touchinit(int w, int h) {
    pthread_t inputThread;
    touch.max_x = w;
    touch.max_y = h;
    return pthread_create(&inputThread, NULL, &eventThread, NULL);
}

void getTouches(int captures, int width, int height, int &cursorx, int &cursory, VGImage &CursorBuffer) {
    int changeCount = 0;
    POINT pointCorrected, pointRaw;
    int ztouch_thold = 50;
    
    do {
        usleep(10000); // usec - slow loop for other threads
        pointRaw.x = touch.x;
        pointRaw.y = touch.y;
        getDisplayPoint(&pointCorrected,&pointRaw,&calibMatrix);
        
        // Loop until a touch is registered by a change in cursor value
        if ((pointCorrected.x != cursorx || pointCorrected.y != cursory) && ((int)touch.z) > ztouch_thold ) {
            //fprintf(stdout,"%d != %d || %d != %d) && %d > %d\n",
             //       pointCorrected.x, cursorx , pointCorrected.y, cursory,  ((int)touch.z) , ztouch_thold);
            restoreCursor(CursorBuffer);
            cursorx = pointCorrected.x;
            cursory = pointCorrected.y;
            saveCursor(CursorBuffer, cursorx, cursory, width, height, CUR_SIZ);
            circleCursor(cursorx, cursory, width, height, CUR_SIZ);
            changeCount = changeCount + 1;
            touch.z = 0.0;
            End();                           // update picture
        }
    } while (changeCount < captures);
}

void drawBackground(int width, int height) {
    Background(0, 0, 0);                   // Black background
    Fill(44, 77, 232, 1);                   // Big blue marble
    Circle(width / 2, 0, width);               // The "world"
    Fill(255, 255, 255, 1);                   // White text
    TextMid(width / 2, height / 2, "Screen Calibration", SerifTypeface, width / 15);    // Greetings
    End();                           // update picture
}

void drawCrosshair(int width, int height, int i) {
    Stroke(255, 255, 255, 0.5);
    StrokeWidth(2);
    Line(  ( xHairLines[i][0] < 0 ? width + xHairLines[i][0] : xHairLines[i][0] )
         , ( xHairLines[i][1] < 0 ? height + xHairLines[i][1] : xHairLines[i][1] )
         , ( xHairLines[i][2] < 0 ? width + xHairLines[i][2] : xHairLines[i][2] )
         , ( xHairLines[i][3] < 0 ? height + xHairLines[i][3] : xHairLines[i][3] )
         );
    
    Line(  ( xHairLines[i+1][0] < 0 ? width + xHairLines[i+1][0] : xHairLines[i+1][0] )
         , ( xHairLines[i+1][1] < 0 ? height + xHairLines[i+1][1] : xHairLines[i+1][1] )
         , ( xHairLines[i+1][2] < 0 ? width + xHairLines[i+1][2] : xHairLines[i+1][2] )
         , ( xHairLines[i+1][3] < 0 ? height + xHairLines[i+1][3] : xHairLines[i+1][3] )
         );
    End();                           // update picture
}

int main() {
    int width, height, cursorx, cursory, cbsize;
    
    init(&width, &height);                   // Graphics initialization
    cursorx = width / 2;
    cursory = height / 2;
    cbsize = (CUR_SIZ * 2) + 1;
    VGImage CursorBuffer = vgCreateImage(VG_sABGR_8888, cbsize, cbsize, VG_IMAGE_QUALITY_BETTER);
    
    if (touchinit(width, height) != 0) {
        fprintf(stderr, "Unable to initialize the touch\n");
        exit(1);
    }
    Start(width, height);                   // Start the picture
    drawBackground(width,height);

    // Set curser values initially
    setCalibrationMatrix( (POINT*)&pointTargets, (POINT*)&pointTargets, &calibMatrix); // initialized 1:1 matrix
    fprintf(stdout,"M: %d,%d,%d,%d,%d,%d,%d\r\n"
            ,calibMatrix.An,calibMatrix.Bn,calibMatrix.Cn,calibMatrix.Dn,calibMatrix.En,calibMatrix.Fn,calibMatrix.Divider );

    // Draw lines
    for( int i = 0; i < xHairLines_ROWS - 2; ) {
        drawCrosshair(width,height,i);
        
        getTouches(1,width,height,cursorx,cursory,CursorBuffer);
        fprintf(stdout,"p:%d %d, %d \r\n",i, cursorx,cursory);
        
        // save touch points
        pointTouches[i/2].x = cursorx;
        pointTouches[i/2].y = cursory;
        // Update for screen dimensions
        pointTargets[i/2].x = (pointTargets[i/2].x < 0 ? width + pointTargets[i/2].x : pointTargets[i/2].x);
        pointTargets[i/2].y = (pointTargets[i/2].y < 0 ? height + pointTargets[i/2].y : pointTargets[i/2].y);
        
        // Conditionally increment to next sample point
        switch(i) {
            case 0:
                i = i + 2 * ( (cursorx < width / 2) && (cursory < height / 2) );
                break;
            case 2:
                i = i + 2 * ( (cursorx > width / 2) && (cursory < height / 2) );
                break;
            case 4:
                i = i + 2 * ( (cursorx < width / 2) && (cursory > height / 2) );
                break;
            default:
                i = i + 2;
        }
    }

    setCalibrationMatrix( (POINT*)&pointTargets, (POINT*)&pointTargets, &calibMatrix); // update matrix
    fprintf(stdout,"M: %d,%d,%d,%d,%d,%d,%d\r\n"
            ,calibMatrix.An,calibMatrix.Bn,calibMatrix.Cn,calibMatrix.Dn,calibMatrix.En,calibMatrix.Fn,calibMatrix.Divider );

    // MAIN LOOP - show some more points
    getTouches(10,width,height,cursorx,cursory,CursorBuffer);
    
    //restoreCursor(CursorBuffer);               // not strictly necessary as display will be closed
    //End();
    vgDestroyImage(CursorBuffer);               // tidy up memory
    finish();                       // Graphics cleanup
    exit(0);
}
