#ifndef _frames_h
#define _frames_h

void getframes(int *sig, int *sig_len, int fs, int win_len, int win_hop){
    int frame_length = win_len * fs;
    int frame_hop = win_hop * fs;
    int overlap = frame_length - frame_hop;

}

#endif 