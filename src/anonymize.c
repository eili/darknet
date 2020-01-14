#include "network.h"
#include "detection_layer.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "image.h"
#include "anonymize.h"
#include "detection_store.h"


#ifdef WIN32
#include <time.h>
#include "gettimeofday.h"
#else
#include <sys/time.h>
#endif


#ifdef OPENCV

#include "http_stream.h"

static char** demo_names;
static image** demo_alphabet;
static int demo_classes;

static int nboxes = 0;
static detection* dets = NULL;

static network net;
static image in_s;
static image det_s;

static cap_cv* cap;
static float fps = 0;
static float demo_thresh = 0;
static int demo_ext_output = 0;
static long long int frame_id = 0;
static int demo_json_port = -1;

#define NFRAMES 3

static float* predictions[NFRAMES];
static int demo_index = 0;
static image images[NFRAMES];
static mat_cv* cv_images[NFRAMES];
static float* avg;

mat_cv* in_img;
mat_cv* det_img;
mat_cv* show_img;

static volatile int flag_exit;
static int letter_box = 0;

//These are defined in demo.c.

void* anon_fetch_in_thread(void* ptr)
{
    int dont_close_stream = 0;    // set 1 if your IP-camera periodically turns off and turns on video-stream
    if (letter_box)
        in_s = get_image_from_stream_letterbox(cap, net.w, net.h, net.c, &in_img, dont_close_stream);
    else
        in_s = get_image_from_stream_resize(cap, net.w, net.h, net.c, &in_img, dont_close_stream);
    if (!in_s.data) {
        printf("Stream closed.\n");
        flag_exit = 1;
        //exit(EXIT_FAILURE);
        return 0;
    }
    //in_s = resize_image(in, net.w, net.h);

    return 0;
}

void* anon_detect_in_thread(void* ptr)
{
    layer l = net.layers[net.n - 1];
    float* X = det_s.data;
    float* prediction = network_predict(net, X);

    memcpy(predictions[demo_index], prediction, l.outputs * sizeof(float));
    mean_arrays(predictions, NFRAMES, l.outputs, avg);
    l.output = avg;

    free_image(det_s);

    cv_images[demo_index] = det_img;
    det_img = cv_images[(demo_index + NFRAMES / 2 + 1) % NFRAMES];
    demo_index = (demo_index + 1) % NFRAMES;

    if (letter_box)
        dets = get_network_boxes(&net, get_width_mat(in_img), get_height_mat(in_img), demo_thresh, demo_thresh, 0, 1, &nboxes, 1); // letter box
    else
        dets = get_network_boxes(&net, net.w, net.h, demo_thresh, demo_thresh, 0, 1, &nboxes, 0); // resized

    return 0;
}

double anon_get_wall_time()
{
    struct timeval walltime;
    if (gettimeofday(&walltime, NULL)) {
        return 0;
    }
    return (double)walltime.tv_sec + (double)walltime.tv_usec * .000001;
}

detection* createDummyDetections(int num, float startX, float startY)
{
    detection* detPtr = NULL;
    detPtr = (detection*)calloc(num, sizeof(detection));
    if (detPtr == NULL)
        exit(0);
    int nclasses = 1;

    for (int i = 0; i < num; i++)
    {
        box b;
        b.x = startX + i * 0.09;
        b.y = startY;
        b.w = 0.05f;
        b.h = 0.09f;
        float prob = 0.93f;
        detection det;
        det.bbox = b;
        det.classes = 1;
        det.sort_class = 0;
        det.prob = (float*)calloc(nclasses, sizeof(float));
        //  det.mask = (float*)calloc(nclasses, sizeof(float));
        det.prob[0] = prob;
        detPtr[i] = det;
    }
    return detPtr;
}


void anon(char* cfgfile, char* weightfile, float thresh, float hier_thresh, int cam_index, const char* filename, char** names, int classes,
    int frame_skip, char* prefix, char* out_filename, int mjpeg_port, int json_port, int dont_show, int ext_output, int letter_box_in)
{
    letter_box = letter_box_in;
    in_img = det_img = show_img = NULL;
    //skip = frame_skip;
    image** alphabet = load_alphabet();
    int delay = frame_skip;
    demo_names = names;
    demo_alphabet = alphabet;
    demo_classes = classes;
    demo_thresh = thresh;
    demo_ext_output = ext_output;
    demo_json_port = json_port;
    printf("Anonymize\n");
    net = parse_network_cfg_custom(cfgfile, 1, 1);    // set batch=1
    if (weightfile) {
        load_weights(&net, weightfile);
    }
    fuse_conv_batchnorm(net);
    calculate_binary_weights(net);
    srand(2222222);

    if (filename) {
        printf("video file: %s\n", filename);
        cap = get_capture_video_stream(filename);
    }
    else {
        printf("Webcam index: %d\n", cam_index);
        cap = get_capture_webcam(cam_index);
    }

    if (!cap) {
#ifdef WIN32
        printf("Check that you have copied file opencv_ffmpeg340_64.dll to the same directory where is darknet.exe \n");
#endif
        error("Couldn't connect to webcam.\n");
    }

    layer l = net.layers[net.n - 1];
    int j;

    avg = (float*)calloc(l.outputs, sizeof(float));
    for (j = 0; j < NFRAMES; ++j) predictions[j] = (float*)calloc(l.outputs, sizeof(float));
    for (j = 0; j < NFRAMES; ++j) images[j] = make_image(1, 1, 3);

    if (l.classes != demo_classes) {
        printf("Parameters don't match: in cfg-file classes=%d, in data-file classes=%d \n", l.classes, demo_classes);
        getchar();
        exit(0);
    }


    flag_exit = 0;

    pthread_t fetch_thread;
    pthread_t detect_thread;

    anon_fetch_in_thread(0);
    det_img = in_img;
    det_s = in_s;

    anon_fetch_in_thread(0);
    anon_detect_in_thread(0);
    det_img = in_img;
    det_s = in_s;

    for (j = 0; j < NFRAMES / 2; ++j) {
        anon_fetch_in_thread(0);
        anon_detect_in_thread(0);
        det_img = in_img;
        det_s = in_s;
    }

    int count = 0;
    if (!prefix && !dont_show) {
        int full_screen = 0;
        create_window_cv("Anonymizer", full_screen, 1352, 1013);
    }


    write_cv* output_video_writer = NULL;
    if (out_filename && !flag_exit)
    {
        int src_fps = 25;
        src_fps = get_stream_fps_cpp_cv(cap);
        output_video_writer =
            create_video_writer(out_filename, 'D', 'I', 'V', 'X', src_fps, get_width_mat(det_img), get_height_mat(det_img), 1);

        //'H', '2', '6', '4'
        //'D', 'I', 'V', 'X'
        //'M', 'J', 'P', 'G'
        //'M', 'P', '4', 'V'
        //'M', 'P', '4', '2'
        //'X', 'V', 'I', 'D'
        //'W', 'M', 'V', '2'
    }

    double before = anon_get_wall_time();
    detectionStore* detStore = CreateStore();

    int k = 0;

    while (1) {
        ++count;
        {
            if (pthread_create(&fetch_thread, 0, anon_fetch_in_thread, 0)) error("Thread creation failed");
            if (pthread_create(&detect_thread, 0, anon_detect_in_thread, 0)) error("Thread creation failed");

            float nms = .45;    // 0.4F
            int local_nboxes = nboxes;
            detection* local_dets = dets;

            if (nms) do_nms_sort(local_dets, local_nboxes, l.classes, nms);

            //printf("Objects:\n");

            /*   if (demo_json_port > 0) {
                   int timeout = 400000;
                   send_json(local_dets, local_nboxes, l.classes, demo_names, frame_id, demo_json_port, timeout);
               }
   */
            int* nDetPtr = &local_nboxes;
            //printf("calling merge: %d\n", detStore->storeLength);
            merge(dets, nDetPtr, detStore, 10);

            //printf("calling draw_detections_blurred_cv_v4: %d\n", detStore->storeLength);
            draw_detections_blurred_cv_v4(show_img, detStore->store, detStore->storeLength, demo_thresh, demo_names, demo_classes, demo_ext_output);

            free_detections(local_dets, local_nboxes);

            //printf("\nFPS:%.1f\n", fps);

            if (!prefix) {
                if (!dont_show) {
                    show_image_mat(show_img, "Anonymize");
                    int c = wait_key_cv(1);
                    if (c == 10) {
                        if (frame_skip == 0) frame_skip = 60;
                        else if (frame_skip == 4) frame_skip = 0;
                        else if (frame_skip == 60) frame_skip = 4;
                        else frame_skip = 0;
                    }
                    else if (c == 27 || c == 1048603) // ESC - exit (OpenCV 2.x / 3.x)
                    {
                        flag_exit = 1;
                    }
                }
            }
            else {
                char buff[256];
                sprintf(buff, "%s_%08d.jpg", prefix, count);
                if (show_img) save_cv_jpg(show_img, buff);
            }

            // save video file
            if (output_video_writer && show_img) {
                write_frame_cv(output_video_writer, show_img);
                printf("\n cvWriteFrame \n");
            }

            release_mat(&show_img);

            pthread_join(fetch_thread, 0);
            pthread_join(detect_thread, 0);

            if (flag_exit == 1) break;

            if (delay == 0) {
                show_img = det_img;
            }
            det_img = in_img;
            det_s = in_s;
        }
        --delay;
        if (delay < 0) {
            delay = frame_skip;

            //double after = get_wall_time();
            //float curr = 1./(after - before);
            double after = get_time_point();    // more accurate time measurements
            float curr = 1000000. / (after - before);
            fps = curr;
            before = after;
        }
    }
    printf("input video stream closed. \n");
    if (output_video_writer) {
        release_video_writer(&output_video_writer);
        printf("output_video_writer closed. \n");
    }

    // free memory
    release_mat(&show_img);
    release_mat(&in_img);
    free_image(in_s);
    freeStore(detStore, demo_classes);
    free(avg);
    for (j = 0; j < NFRAMES; ++j) free(predictions[j]);
    for (j = 0; j < NFRAMES; ++j) free_image(images[j]);

    free_ptrs((void**)names, net.layers[net.n - 1].classes);

    int i;
    const int nsize = 8;
    for (j = 0; j < nsize; ++j) {
        for (i = 32; i < 127; ++i) {
            free_image(alphabet[j][i]);
        }
        free(alphabet[j]);
    }
    free(alphabet);
    free_network(net);
    //cudaProfilerStop();
}


/*
void test_detector(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh,
    float hier_thresh, int dont_show, int ext_output, int save_labels, char *outfile, int letter_box)
*/
void anonfiles(char* datacfg, char* cfgfile, char* weightfile,  float thresh, float hier_thresh,  const char* filename, char** names, int classes,
 char* prefix, char* out_filename,  int dont_show, int ext_output, int letter_box_in, int save_labels, char* outfile)
{
    printf("Inside\n");
    demo_names = names;
    demo_classes = classes;
    demo_thresh = thresh;
    demo_ext_output = ext_output;

    list* options = read_data_cfg(datacfg);

    image** alphabet = load_alphabet();
    network net = parse_network_cfg_custom(cfgfile, 1, 1); // set batch=1
    if (weightfile) {
        load_weights(&net, weightfile);
    }
    fuse_conv_batchnorm(net);
    calculate_binary_weights(net);
    if (net.layers[net.n - 1].classes != demo_classes) {
        printf(" Error: in the file %s number of names %d that isn't equal to classes=%d in the file %s \n",
            demo_names, demo_classes, net.layers[net.n - 1].classes, cfgfile);
        if (net.layers[net.n - 1].classes > demo_classes) getchar();
    }
    srand(2222222);
    char buff[256];
    char* input = buff;
    char* json_buf = NULL;
    int json_image_id = 0;
    FILE* json_file = NULL;
    if (outfile) {
        json_file = fopen(outfile, "wb");
        char* tmp = "[\n";
        fwrite(tmp, sizeof(char), strlen(tmp), json_file);
    }
    int j;
    float nms = .45;    // 0.4F
    
    while (1) {
        if (filename) {

            strncpy(input, filename, 256);
            if (strlen(input) > 0)
                if (input[strlen(input) - 1] == 0x0d) input[strlen(input) - 1] = 0;
        }
        else {
            printf("Enter Image Path: ");
            fflush(stdout);
            input = fgets(input, 256, stdin);
            if (!input) break;
            strtok(input, "\n");
        }
        printf("\ninput\n");
        printf(input);
        printf("\n");

        //image im;
        //image sized = load_image_resize(input, net.w, net.h, net.c, &im);
        image im = load_image(input, 0, 0, net.c);
        image sized;
        if (letter_box) sized = letterbox_image(im, net.w, net.h);
        else sized = resize_image(im, net.w, net.h);
        layer l = net.layers[net.n - 1];


        float* X = sized.data;

        //time= what_time_is_it_now();
        double time = get_time_point();
        network_predict(net, X);
        //network_predict_image(&net, im); letterbox = 1;
        printf("%s: Predicted in %lf milli-seconds.\n", input, ((double)get_time_point() - time) / 1000);

        int nboxes = 0;
        detection* dets = get_network_boxes(&net, im.w, im.h, thresh, hier_thresh, 0, 1, &nboxes, letter_box);

        if (nms) do_nms_sort(dets, nboxes, l.classes, nms);
        
        //draw_detections_v3(im, dets, nboxes, thresh, demo_names, alphabet, l.classes, ext_output);
        printf("1\n");
        mat_cv* outImg = draw_detections_blurred_cv_v3(im, dets, nboxes, demo_thresh, demo_names, demo_classes, demo_ext_output);
        printf("2\n");

        const char* outfilename = makeResultFilestring(input, "results");
        //const char* strippedFilename = stripExtension(outfilename);
        printf("saving file %s\n", outfilename);
        saveImage(outImg, outfilename);

        //save_image(im, strippedFilename);
        //save_image(im, "predictions");
        if (!dont_show) {
            //show_image(im, "predictions");
           
            show_image_mat(outImg, "Anonymize");
        }

        if (outfile) {
            if (json_buf) {
                char* tmp = ", \n";
                fwrite(tmp, sizeof(char), strlen(tmp), json_file);
            }
            ++json_image_id;
            json_buf = detection_to_json(dets, nboxes, l.classes, demo_names, json_image_id, input);

            fwrite(json_buf, sizeof(char), strlen(json_buf), json_file);
            free(json_buf);
        }

        // pseudo labeling concept - fast.ai
        if (save_labels)
        {
            char labelpath[4096];
            replace_image_to_label(input, labelpath);

            FILE* fw = fopen(labelpath, "wb");
            int i;
            for (i = 0; i < nboxes; ++i) {
                char buff[1024];
                int class_id = -1;
                float prob = 0;
                for (j = 0; j < l.classes; ++j) {
                    if (dets[i].prob[j] > thresh&& dets[i].prob[j] > prob) {
                        prob = dets[i].prob[j];
                        class_id = j;
                    }
                }
                if (class_id >= 0) {
                    sprintf(buff, "%d %2.4f %2.4f %2.4f %2.4f\n", class_id, dets[i].bbox.x, dets[i].bbox.y, dets[i].bbox.w, dets[i].bbox.h);
                    fwrite(buff, sizeof(char), strlen(buff), fw);
                }
            }
            fclose(fw);
        }

        free_detections(dets, nboxes);
        free_image(im);
        free_image(sized);

        if (!dont_show) {
            wait_until_press_key_cv();
            destroy_all_windows_cv();
        }

        if (filename) break;
    }

    if (outfile) {
        char* tmp = "\n]";
        fwrite(tmp, sizeof(char), strlen(tmp), json_file);
        fclose(json_file);
    }

    // free memory
    free_ptrs((void**)names, net.layers[net.n - 1].classes);
    free_list_contents_kvp(options);
    free_list(options);

    int i;
    const int nsize = 8;
    for (j = 0; j < nsize; ++j) {
        for (i = 32; i < 127; ++i) {
            free_image(alphabet[j][i]);
        }
        free(alphabet[j]);
    }
    free(alphabet);

    free_network(net);
}

#else
void demo(char* cfgfile, char* weightfile, float thresh, float hier_thresh, int cam_index, const char* filename, char** names, int classes,
    int frame_skip, char* prefix, char* out_filename, int mjpeg_port, int json_port, int dont_show, int ext_output, int letter_box_in)
{
    fprintf(stderr, "Anonymizer needs OpenCV for webcam images.\n");
}
#endif
