#include "detection_store.h"
#include <Windows.h>

float boxArea(box b) {
    return b.w * b.h;
}

point boxCenter(box b) {
    point p;
    p.x = b.x + b.w / 2;
    p.y = b.y + b.h / 2;

    return p;
}

float boxRadius(box b)
{
    float a = pow(b.w / 2, 2) + pow(b.h / 2, 2);
    float r = sqrt(a) / 2;
    return r;
}

//If this diff is positive, then the boxes are too far from each other to overlap. They are then separate boxes.
float boxCompare(box b1, box b2)
{
    float r1 = boxRadius(b1);
    float r2 = boxRadius(b2);
    float r1r2 = r1 + r2;
    float c1c2 = sqrt(pow(b1.x - b2.x, 2) + pow(b1.y - b2.y, 2));

    float diff = c1c2 - r1r2;
    return diff;
}


detectionStore* CreateStore()
{
    detectionStore* detStorePtr = NULL;
    detStorePtr = (detectionStore*)malloc(sizeof(detectionStore));
    if (detStorePtr == NULL)
        exit(0);
    detStorePtr->maxStoreCapacity = 200;
    detStorePtr->storeLength = 0;

    return detStorePtr;
}

detection* makeDeepCopy(detection* other, int classes)
{
    //printf("makeDeepCopy\n");
    detection* dp = NULL;
    dp = (detection*)malloc(sizeof(detection));
    if (dp == NULL)
    {
        printf("malloc failed for struct detection");
        exit(0);
    }
    float* probp = (float*)calloc(classes, sizeof(float));
    if (probp == NULL)
    {
        printf("malloc failed for float prob pointer");
        exit(0);
    }
    memcpy(probp, other->prob, classes * sizeof(float));
    /*memcpy(dp->mask, other->mask, sizeof(float));*/
    memcpy(dp, other, sizeof(detection));
    dp->prob = probp;
    return dp;
}

detectedObj createObject(detection detection)
{
    detectedObj obj;
    obj.det = detection;
    obj.missCount = 0;
    obj.deltaX = 0;
    obj.deltaY = 0;

    return obj;
}

//compares if the float f1 is equal with f2 and returns 1 if true and 0 if false
//https://how-to.fandom.com/wiki/Howto_compare_floating_point_numbers_in_the_C_programming_language
int compare_float(float f1, float f2)
{
    float precision = 0.001;
    if (((f1 - precision) < f2) &&
        ((f1 + precision) > f2))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}


void merge(detection* dets, int* num, detectionStore* detStore, int maxMemCount)
{
    printf("merge %d\n", *num);
    const int buffersize = 100;
    detectedObj toAddFromMemory[100];
    int currentStoreLength = detStore->storeLength;
    int tmpCounter = 0;
    //See if there are any detections in Store that are missing in the current detection list (this video frame).
    //Add to temporary array if counter not above limit.
    for (int i = 0; i < detStore->storeLength; i++)
    {
        //printf("i=%d,", i);
        detectedObj memDet = detStore->store[i];
        if (memDet.missCount > maxMemCount)
            continue;
        int found = 0;
        for (int j = 0; j < *num; j++)
        {
            //printf("\nj=%d,", j);
            detection det = dets[j];

            float cmp = boxCompare(det.bbox, memDet.det.bbox);
            if (cmp < 0) {
                //We say this is the same object.

                found = 1;
                memDet.missCount = 0;
                //break out from the inner loop as we have found a match
                break;
            }
        }
        if (found == 0)
        {
            printf("detection missing, add to store\n");
            if (tmpCounter < buffersize) {
                memDet.missCount++;
                toAddFromMemory[tmpCounter++] = memDet;
            }
        }
    }

    int newLength = *num + tmpCounter;
    printf("B %d\n", newLength);
    //Create memory to consist of the current detections + old ones added
    detectedObj* memP = NULL;
    memP = (detectedObj*)calloc(newLength, sizeof(detectedObj));
    if (memP == NULL) {
        printf("Failed allocating memory\n");
        exit(0);
    }

    //This adds the current detections to memory, by making a deep copy of each detection and creating a
    //detectionObj struct to go into memory.
    for (int i = 0; i < *num; i++)
    {
        printf("i=%d, ", i);
        detection det = dets[i];
        detection* copiedDet = makeDeepCopy(&det, 1);
        memP[i].det = *copiedDet;
        memP[i].missCount = 0;
    }


    for (int i = 0; i < tmpCounter; i++)
    {
        detectedObj tmp = toAddFromMemory[i];
        detection* copiedDet = makeDeepCopy(&tmp.det, 1);
        detectedObj tmp2;
        tmp2.det = *copiedDet;
        tmp2.missCount = tmp.missCount;
        memP[*num + i] = tmp2;
    }
    //printf("E\n");


    detStore->store = memP;
    detStore->storeLength = newLength;
}

/*
New detection list will consist of basically the current detection list + any from the memory that is not found in current.
If a detection fetched from memory has got a high number count, remove it.
Algorithm:
For each item in memory
  for each item in detection list
    compare memory and detection
  if item from memory is not found in detections, then add it to detections
Current detections is copied to memory, old memory is cleared.
*/
void merge2(detection* dets, int* num, detectionStore* detStore, int maxMemCount)
{
    printf("merge %d\n", *num);
    const int buffersize = 100;
    detectedObj toAddFromMemory[100];
    int currentStoreLength = detStore->storeLength;
    int tmpCounter = 0;
    //See if there are any detections in Store that are missing in the current detection list (this video frame).
    //Add to temporary array if counter not above limit.
    for (int i = 0; i < detStore->storeLength; i++)
    {
        //printf("i=%d,", i);
        detectedObj memDet = detStore->store[i];
        if (memDet.missCount > maxMemCount)
            continue;
        int found = 0;
        for (int j = 0; j < *num; j++)
        {
            //printf("\nj=%d,", j);
            detection det = dets[j];

            float cmp = boxCompare(det.bbox, memDet.det.bbox);
            if (cmp < 0) {
                //We say this is the same object.

                found = 1;
                detectedObj tmp = createObject(det);
               /* tmp.deltaX = det.bbox.x - memDet.det.bbox.x;
                tmp.deltaY = det.bbox.y - memDet.det.bbox.y;*/
                if (tmpCounter < buffersize) {
                    toAddFromMemory[tmpCounter++] = tmp;
                }
                //break out from the inner loop as we have found a match
                break;
            }
        }
        if (found == 0)
        {
            printf("detection exists in store but missing in current detection list, keep it in store\n");
            if (tmpCounter < buffersize) {
                memDet.missCount++;
                toAddFromMemory[tmpCounter++] = memDet;
            }
        }
    }
    freeStore(detStore);

    //Here we only add new detections to the store
    //Note that we cannot add to the already existing temporary list as we use it in the inner too to search from.
    //Also not that we just adds the detection to this list, not the detectionObj struct.
    detection newToMemory[100];
    int newToMemoryCounter = 0;
    for (int i = 0; i < *num; i++)
    {
        detection det = dets[i];
        boolean found = FALSE;
        for (int j = 0; j < tmpCounter; j++) {
            detectedObj tmp = toAddFromMemory[j];
            //Use compare_float to compare floats
            //if (tmp.det.bbox.x == det.bbox.x && tmp.det.bbox.y == det.bbox.y)

            if (compare_float(tmp.det.bbox.x, det.bbox.x) ==1 && compare_float(tmp.det.bbox.y, det.bbox.y)==1)
            {
                //Already in the temporary list of detections, we do not add it again
                printf("found, %d, %f. \n", j, tmp.deltaX);
                found = TRUE;
                break;
            }
        }
        if (found == FALSE)
        {
            float w = det.bbox.w;
            //Is it smart to set a lower limit on box size? 0.01 fraction of image width
            if (w > 0.01) {
                if (newToMemoryCounter < buffersize)
                {
                    newToMemory[newToMemoryCounter++] = det;
                }
            }
        }
    }

    int finalLength = tmpCounter + newToMemoryCounter;
    //Create memory to consist of the current detections + old ones added
    detectedObj* memP = NULL;
    memP = (detectedObj*)calloc(finalLength, sizeof(detectedObj));
    if (memP == NULL)
        exit(0);


    //First iterate the toAddFromMemory array. These have already been put into the store.
    for (int i = 0; i < tmpCounter; i++)
    {
        detectedObj tmp = toAddFromMemory[i];

        detection* copiedDet = makeDeepCopy(&tmp.det, 1);
        tmp.det = *copiedDet;
        memP[i] = tmp;
    }

    //These are the new detections, not already in store
    for (int i = 0; i < newToMemoryCounter; i++)
    {
        detection det = newToMemory[i];
        detection* copiedDet = makeDeepCopy(&det, 1);
        detectedObj tmp = createObject(*copiedDet);
        memP[tmpCounter + i] = tmp;
    }

    detStore->store = memP;
    detStore->storeLength = finalLength;

}

void printStore(detectionStore* detStore)
{
    printf("printStore\n");
    printf("Store: %d\n", detStore->storeLength);

    for (int i = 0; i < detStore->storeLength; i++)
    {
        detectedObj di = detStore->store[i];
        printf("x,y,prob,count: %f, %f, %f, %d\n", di.det.bbox.x, di.det.bbox.y, di.det.prob[0], di.missCount);
    }
}


void freeDetections(detection* detections, int length)
{
    if (length > 0) {
        for (int i = 0; i < length; i++) {
            free(detections[i].prob);
        }
        free(detections);
    }
}
void freeStore(detectionStore* detStore)
{
    if (detStore->storeLength > 0)
    {
        for (int i = 0; i < detStore->storeLength; i++)
        {
            detectedObj detObj = detStore->store[i];
            freeDetections(&detObj.det, 1);
        }
    }
}