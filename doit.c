#include <windows.h>
#include <winsock.h>

#include <stdio.h>
#include <string.h>

/*
 * Export the application name.
 */
char const *listener_appname = "DoIt";

/*
 * Export the list of ports to listen on.
 */
static int const port_array[] = { 6666 };
int listener_nports = sizeof(port_array) / sizeof(*port_array);
int const *listener_ports = port_array;

/*
 * Export the function that handles a connection.
 */
int listener_newthread(SOCKET sock, int port, SOCKADDR_IN remoteaddr) {
    char *cmdline = NULL;
    int cmdlen = 0, cmdsize = 0;
    char buf[64];
    int len, newlen;

    while (1) {
        len = recv(sock, buf, sizeof(buf), 0);
        if (cmdsize < cmdlen + len + 1) {
            cmdsize = cmdlen + len + 1 + 256;
            cmdline = realloc(cmdline, cmdsize);
            if (!cmdline) {
                closesocket(sock);
                return 0;
            }
        }
        memcpy(cmdline+cmdlen, buf, len);
        cmdline[cmdlen+len] = '\0';
        newlen = strcspn(buf, "\r\n");
        if (newlen < len) {
            buf[newlen] = '\0';
            break;
        }
    }
    MessageBox(NULL, buf, "Message!", MB_OK);
    send(sock, "+ok\r\n", 5, 0);
    closesocket(sock);
    return 0;
}

/*
 * Export the function that gets the command line.
 */
void listener_cmdline(char *cmdline) {
}

/* 
 * ======================================================================
 * SHA implementation, for authentication.
 */

typedef unsigned int uint32;

/* ----------------------------------------------------------------------
 * Core SHA algorithm: processes 16-word blocks into a message digest.
 */

#define rol(x,y) ( ((x) << (y)) | (((uint32)x) >> (32-y)) )

typedef struct {
    uint32 h[5];
} SHA_Core_State;

void SHA_Core_Init(SHA_Core_State *s) {
    s->h[0] = 0x67452301;
    s->h[1] = 0xefcdab89;
    s->h[2] = 0x98badcfe;
    s->h[3] = 0x10325476;
    s->h[4] = 0xc3d2e1f0;
}

void SHA_Block(SHA_Core_State *s, uint32 *block) {
    uint32 w[80];
    uint32 a,b,c,d,e;
    int t;

    for (t = 0; t < 16; t++)
        w[t] = block[t];

    for (t = 16; t < 80; t++) {
        uint32 tmp = w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16];
        w[t] = rol(tmp, 1);
    }

    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];

    for (t = 0; t < 20; t++) {
        uint32 tmp = rol(a, 5) + ( (b&c) | (d&~b) ) + e + w[t] + 0x5a827999;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    for (t = 20; t < 40; t++) {
        uint32 tmp = rol(a, 5) + (b^c^d) + e + w[t] + 0x6ed9eba1;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    for (t = 40; t < 60; t++) {
        uint32 tmp = rol(a, 5) + ( (b&c) | (b&d) | (c&d) ) + e + w[t] + 0x8f1bbcdc;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    for (t = 60; t < 80; t++) {
        uint32 tmp = rol(a, 5) + (b^c^d) + e + w[t] + 0xca62c1d6;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }

    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

/* ----------------------------------------------------------------------
 * Outer SHA algorithm: take an arbitrary length byte string,
 * convert it into 16-word blocks with the prescribed padding at
 * the end, and pass those blocks to the core SHA algorithm.
 */

#define BLKSIZE 64

typedef struct {
    SHA_Core_State core;
    unsigned char block[BLKSIZE];
    int blkused;
    uint32 lenhi, lenlo;
} SHA_State;

void SHA_Init(SHA_State *s) {
    SHA_Core_Init(&s->core);
    s->blkused = 0;
    s->lenhi = s->lenlo = 0;
}

void SHA_Bytes(SHA_State *s, void *p, int len) {
    unsigned char *q = (unsigned char *)p;
    uint32 wordblock[16];
    uint32 lenw = len;
    int i;

    /*
     * Update the length field.
     */
    s->lenlo += lenw;
    s->lenhi += (s->lenlo < lenw);

    if (s->blkused && s->blkused+len < BLKSIZE) {
        /*
         * Trivial case: just add to the block.
         */
        memcpy(s->block + s->blkused, q, len);
        s->blkused += len;
    } else {
        /*
         * We must complete and process at least one block.
         */
        while (s->blkused + len >= BLKSIZE) {
            memcpy(s->block + s->blkused, q, BLKSIZE - s->blkused);
            q += BLKSIZE - s->blkused;
            len -= BLKSIZE - s->blkused;
            /* Now process the block. Gather bytes big-endian into words */
            for (i = 0; i < 16; i++) {
                wordblock[i] =
                    ( ((uint32)s->block[i*4+0]) << 24 ) |
                    ( ((uint32)s->block[i*4+1]) << 16 ) |
                    ( ((uint32)s->block[i*4+2]) <<  8 ) |
                    ( ((uint32)s->block[i*4+3]) <<  0 );
            }
            SHA_Block(&s->core, wordblock);
            s->blkused = 0;
        }
        memcpy(s->block, q, len);
        s->blkused = len;
    }
}

void SHA_Final(SHA_State *s, uint32 *output) {
    int i;
    int pad;
    unsigned char c[64];
    uint32 lenhi, lenlo;

    if (s->blkused >= 56)
        pad = 56 + 64 - s->blkused;
    else
        pad = 56 - s->blkused;

    lenhi = (s->lenhi << 3) | (s->lenlo >> (32-3));
    lenlo = (s->lenlo << 3);

    memset(c, 0, pad);
    c[0] = 0x80;
    SHA_Bytes(s, &c, pad);

    c[0] = (lenhi >> 24) & 0xFF;
    c[1] = (lenhi >> 16) & 0xFF;
    c[2] = (lenhi >>  8) & 0xFF;
    c[3] = (lenhi >>  0) & 0xFF;
    c[4] = (lenlo >> 24) & 0xFF;
    c[5] = (lenlo >> 16) & 0xFF;
    c[6] = (lenlo >>  8) & 0xFF;
    c[7] = (lenlo >>  0) & 0xFF;

    SHA_Bytes(s, &c, 8);

    for (i = 0; i < 5; i++)
        output[i] = s->core.h[i];
}

void SHA_Simple(void *p, int len, uint32 *output) {
    SHA_State s;

    SHA_Init(&s);
    SHA_Bytes(&s, p, len);
    SHA_Final(&s, output);
}
