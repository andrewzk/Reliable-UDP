#define VS_MINLEN    4
#define VS_FILENAMELENGTH 128
#define VS_MAXDATA    128

#define VS_TYPE_BEGIN    1
#define VS_TYPE_DATA    2
#define VS_TYPE_END     3

struct vsftp {
  u_int32_t vs_type;
  union {
    char vs_filename[VS_FILENAMELENGTH];
    u_int8_t vs_data[VS_MAXDATA];
  } vs_info;
};
