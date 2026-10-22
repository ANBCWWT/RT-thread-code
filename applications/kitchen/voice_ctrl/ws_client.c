/*
 * 轻量 WebSocket 客户端实现（语音控制 · 阶段 2）
 *
 * RFC6455 握手 + 帧编解码，明文 ws。加密组件复用 mbedtls
 * （工程已启用 packages/mbedtls-v2.28.1）。
 */
#include "ws_client.h"
#include "asr_config.h"

#include <rtthread.h>
#include <rtdevice.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <string.h>
#include <stdio.h>

#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

#define DBG_TAG "voice.ws"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define WS_GUID              "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_HANDSHAKE_TIMEOUT_MS  5000

/* ---------------- 内部工具 ---------------- */

/* 精确读取 n 字节（阻塞，受 SO_RCVTIMEO 约束） */
static int ws_recv_all(int fd, uint8_t *buf, int n)
{
    int got = 0;

    while (got < n)
    {
        int r = (int)recv(fd, buf + got, n - got, 0);
        if (r <= 0)
        {
            return -1;
        }
        got += r;
    }
    return got;
}

/* xorshift 伪随机（握手 key / 掩码用，demo 足够） */
static rt_uint32_t s_rng_state;

static void ws_rng_init(void)
{
    s_rng_state = (rt_uint32_t)rt_tick_get() ^ 0x9E3779B9U;
}

static rt_uint32_t ws_rng_next(void)
{
    rt_uint32_t x = s_rng_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

static int ws_b64_encode(char *dst, int dst_cap, const uint8_t *src, int src_len)
{
    size_t olen = 0;

    if (mbedtls_base64_encode((unsigned char *)dst, (size_t)dst_cap,
                              &olen, src, (size_t)src_len) != 0)
    {
        return -1;
    }
    return (int)olen;
}

/* ---------------- 握手 ---------------- */

int ws_connect(const char *host, int port, const char *path_query,
               const char *extra_headers, char *err, int err_len)
{
    int fd = -1;
    struct hostent *hp;
    struct sockaddr_in server;
    struct timeval tv;
    rt_uint8_t key_raw[16];
    char key_b64[32];
    char expect_accept[64];
    char request[1024];
    char response[2048];
    char line[256];
    rt_uint8_t sha1_out[20];
    const char *p;
    int n = 0;
    int accept_ok = 0;
    int i;

    if (err && err_len > 0)
    {
        err[0] = '\0';
    }

    /* 随机 Sec-WebSocket-Key */
    ws_rng_init();
    for (i = 0; i < 16; i++)
    {
        key_raw[i] = (rt_uint8_t)(ws_rng_next() & 0xFF);
    }
    if (ws_b64_encode(key_b64, sizeof(key_b64), key_raw, 16) < 0)
    {
        if (err) rt_snprintf(err, err_len, "b64 key fail");
        return -1;
    }

    /* DNS */
    hp = gethostbyname(host);
    if (hp == RT_NULL)
    {
        if (err) rt_snprintf(err, err_len, "dns: %s", host);
        return -1;
    }

    /* TCP */
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (err) rt_snprintf(err, err_len, "socket fail");
        return -1;
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)port);
    memcpy(&server.sin_addr, hp->h_addr, hp->h_length);

    if (connect(fd, (struct sockaddr *)&server, sizeof(server)) != 0)
    {
        if (err) rt_snprintf(err, err_len, "connect %s:%d fail", host, port);
        closesocket(fd);
        return -1;
    }

    /* 请求头 */
    rt_snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: %s\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "%s"
                "\r\n",
                path_query ? path_query : "/", host, key_b64,
                extra_headers ? extra_headers : "");

    if (send(fd, request, (int)rt_strlen(request), 0) <= 0)
    {
        if (err) rt_snprintf(err, err_len, "send handshake fail");
        goto fail;
    }

    /* 读响应头（SO_RCVTIMEO 兜底） */
    tv.tv_sec = WS_HANDSHAKE_TIMEOUT_MS / 1000;
    tv.tv_usec = (WS_HANDSHAKE_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    while (1)
    {
        int r = (int)recv(fd, response + n, (int)sizeof(response) - n - 1, 0);

        if (r <= 0)
        {
            if (err) rt_snprintf(err, err_len, "recv handshake fail/timeout");
            goto fail;
        }
        n += r;
        response[n] = '\0';
        if (strstr(response, "\r\n\r\n") != RT_NULL)
        {
            break;
        }
        if (n >= (int)sizeof(response) - 1)
        {
            if (err) rt_snprintf(err, err_len, "handshake resp too large");
            goto fail;
        }
    }

    /* 状态行 */
    if (strncmp(response, "HTTP/1.1 101", 12) != 0 &&
        strncmp(response, "HTTP/1.0 101", 12) != 0)
    {
        LOG_E("handshake failed, resp: %.120s", response);
        if (err) rt_snprintf(err, err_len, "bad status, resp: %.60s", response);
        goto fail;
    }

    /* 期望的 Sec-WebSocket-Accept */
    {
        char key_guid[64];
        int kl = rt_snprintf(key_guid, sizeof(key_guid), "%s%s", key_b64, WS_GUID);

        mbedtls_sha1((const unsigned char *)key_guid, (size_t)kl, sha1_out);
        if (ws_b64_encode(expect_accept, sizeof(expect_accept), sha1_out, 20) < 0)
        {
            goto fail;
        }
    }

    /* 逐行校验 Sec-WebSocket-Accept */
    p = response;
    while ((p = strstr(p, "\r\n")) != RT_NULL)
    {
        const char *line_start = (p == response) ? response : p + 2;
        const char *line_end = strstr(line_start, "\r\n");

        if (line_end == RT_NULL)
        {
            break;
        }
        if (line_end - line_start > 0 && line_end - line_start < (int)sizeof(line))
        {
            int ll = (int)(line_end - line_start);

            memcpy(line, line_start, ll);
            line[ll] = '\0';
            if (strncmp(line, "Sec-WebSocket-Accept:", 21) == 0)
            {
                const char *v = line + 21;

                while (*v == ' ')
                {
                    v++;
                }
                if (strncmp(v, expect_accept, rt_strlen(expect_accept)) == 0)
                {
                    accept_ok = 1;
                    break;
                }
            }
        }
        p = line_end;
    }

    if (!accept_ok)
    {
        if (err) rt_snprintf(err, err_len, "Sec-WebSocket-Accept mismatch");
        goto fail;
    }

    LOG_I("ws handshake ok, host=%s", host);
    return fd;

fail:
    closesocket(fd);
    return -1;
}

/* ---------------- 发送帧（客户端掩码，就地 XOR） ---------------- */

int ws_send(int fd, int opcode, uint8_t *payload, int len)
{
    uint8_t hdr[14];
    int hlen = 2;
    int i;

    if (len < 0 || len >= 65536)
    {
        return -1;   /* 本实现不支持大帧 */
    }

    hdr[0] = 0x80 | (opcode & 0x0F);       /* FIN=1 */
    if (len < 126)
    {
        hdr[1] = 0x80 | (uint8_t)len;      /* MASK=1 */
    }
    else
    {
        hdr[1] = 0x80 | 126;
        hdr[2] = (uint8_t)(len >> 8);
        hdr[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    }

    /* 4 字节掩码 */
    for (i = 0; i < 4; i++)
    {
        hdr[hlen + i] = (uint8_t)(ws_rng_next() & 0xFF);
    }

    /* 先发 头+掩码，再发掩码后的 payload */
    if (send(fd, hdr, hlen + 4, 0) <= 0)
    {
        return -1;
    }
    if (len > 0)
    {
        uint8_t mask[4];

        memcpy(mask, hdr + hlen, 4);
        for (i = 0; i < len; i++)
        {
            payload[i] ^= mask[i & 3];     /* 就地掩码（缓冲可写） */
        }
        if (send(fd, payload, len, 0) <= 0)
        {
            return -1;
        }
    }
    return len;
}

/* ---------------- 接收帧 ---------------- */

int ws_recv_frame(int fd, uint8_t *payload, int payload_cap,
                  int timeout_ms, int *opcode_out)
{
    uint8_t hdr[2];
    uint8_t ext[8];
    unsigned long long len;
    int opcode;
    int masked;
    int i;

    RT_UNUSED(timeout_ms);

    while (1)
    {
        if (ws_recv_all(fd, hdr, 2) != 2)
        {
            return -1;                      /* 超时或断开 */
        }
        opcode = hdr[0] & 0x0F;
        masked = (hdr[1] & 0x80) ? 1 : 0;
        len = hdr[1] & 0x7F;

        if (len == 126)
        {
            if (ws_recv_all(fd, ext, 2) != 2)
            {
                return -1;
            }
            len = ((unsigned long long)ext[0] << 8) | ext[1];
        }
        else if (len == 127)
        {
            if (ws_recv_all(fd, ext, 8) != 8)
            {
                return -1;
            }
            len = 0;
            for (i = 0; i < 8; i++)
            {
                len = (len << 8) | ext[i];
            }
        }

        if (masked)
        {
            if (ws_recv_all(fd, ext, 4) != 4)
            {
                return -1;
            }
        }

        if (len > (unsigned long long)payload_cap)
        {
            return -3;                      /* 帧超容量 */
        }

        if (opcode == 0x9)                  /* PING → PONG（payload 缓冲复用） */
        {
            if ((int)len > 0 && ws_recv_all(fd, payload, (int)len) != (int)len)
            {
                return -1;
            }
            ws_send(fd, 0xA, payload, (int)len);
            continue;
        }

        if (opcode == 0x8)                  /* CLOSE */
        {
            return -2;
        }

        if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0)
        {
            if ((int)len > 0)
            {
                if (ws_recv_all(fd, payload, (int)len) != (int)len)
                {
                    return -1;
                }
                if (masked)
                {
                    for (i = 0; i < (int)len; i++)
                    {
                        payload[i] ^= ext[i & 3];
                    }
                }
            }
            if (opcode_out)
            {
                *opcode_out = opcode;
            }
            return (int)len;
        }

        /* 其它 opcode（如 continuation 之外的保留类型）：跳过 payload */
        if ((int)len > 0)
        {
            if (ws_recv_all(fd, payload, (int)len) != (int)len)
            {
                return -1;
            }
        }
    }
}

void ws_close(int fd)
{
    if (fd >= 0)
    {
        closesocket(fd);
    }
}
