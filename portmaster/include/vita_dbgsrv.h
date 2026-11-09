#ifndef VITA_DBGSRV_H
#define VITA_DBGSRV_H

#ifdef __cplusplus
extern "C" {
#endif

#define VITA_DBGSRV_PORT 9990

void vita_dbgsrv_start(void);
void vita_dbgsrv_frame(void);
int vita_dbgsrv_take_command(char *out, int size);

typedef struct VitaDbgPad {
    unsigned int  buttons;
    int           has_sticks;
    unsigned char lx, ly, rx, ry;
} VitaDbgPad;

int vita_dbgsrv_pad(VitaDbgPad *out);
void vita_dbgsrv_set_busy(int busy);

#ifdef __cplusplus
}
#endif

#endif // VITA_DBGSRV_H
