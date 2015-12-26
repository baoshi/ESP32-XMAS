#ifndef __BASE64_H__
#define __BASE64_H__

#ifdef __cplusplus
extern "C" {
#endif


int Base64encode_len(int len);

int Base64encode(char *encoded, const char *string, int len);


#ifdef __cplusplus
}
#endif


#endif // __BASE64_H__
