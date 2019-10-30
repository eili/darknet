#pragma once
#include "darknet.h"
#include <math.h>

typedef struct point {
	float x, y;
} point;

typedef struct detectedObj {
	detection det;
    /*
    Number of times this detection has gone missing
    */
	int missCount;
    float deltaX;
    float deltaY;
} detectedObj;

typedef struct detectionStore {
    detectedObj* store;
    int maxStoreCapacity;
    int storeLength;
} detectionStore;


detectionStore* CreateStore();
detection* makeDeepCopy(detection* other, int classes);
void merge(detection* dets, int* num, detectionStore* detStore, int maxMemCount);
void merge2(detection* dets, int* num, detectionStore* detStore, int maxMemCount);
void printStore(detectionStore* detStore);
void freeDetections(detection* detections, int nClasses);
void freeStore(detectionStore* detStore, int nClasses);
int compare_float(float f1, float f2);