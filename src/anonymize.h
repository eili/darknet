#ifndef ANONYMIZE_H
#define ANONYMIZE_H

#include "image.h"
#ifdef __cplusplus
extern "C" {
#endif
	void anon(char* cfgfile, char* weightfile, float thresh, float hier_thresh, int cam_index, const char* filename, char** names, int classes,
		int frame_skip, char* prefix, char* out_filename, int mjpeg_port, int json_port, int dont_show, int ext_output, int letter_box_in);

    void anonfiles(char* datacfg, char* cfgfile, char* weightfile, float thresh, float hier_thresh, const char* filename, char** names, int classes,
        char* prefix, char* out_filename, int dont_show, int ext_output, int letter_box_in, int save_labels, char* outfile);

    //detection* createDummyDetections(int num);
#ifdef __cplusplus
}
#endif

#endif
