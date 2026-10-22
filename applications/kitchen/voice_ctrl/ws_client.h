/*
 * 轻量 WebSocket 客户端（语音控制 · 阶段 2）
 *
 * 自写 RFC6455 客户端，仅实现本需求需要的子集：
 *  - 明文 ws://（80 端口），不做 TLS（demo 够用）
 *  - 握手：Sec-WebSocket-Key / Accept 校验
 *  - 发送：text(0x1) / binary(0x2)，客户端掩码
 *  - 接收：text/binary/continuation 帧，自动回 PONG(0xA)，识别 CLOSE(0x8)
 *  - 不处理分片大帧（>payload_cap 返回 -3）
 *
 * 依赖：lwIP/SAL socket + mbedtls（SHA1 / base64，包已在工程启用）
 */
#ifndef VOICE_WS_CLIENT_H
#define VOICE_WS_CLIENT_H

#include <rtthread.h>

/*
 * 建立 WebSocket 连接。
 * @host          服务器域名
 * @port          TCP 端口（明文用 80）
 * @path_query    请求路径+查询串，如 "/v2/iat?authorization=..."
 * @extra_headers 附加 HTTP 头（可选，RT_NULL 或 "\r\n" 结尾），
 *                讯飞签名已编码进 path_query，通常传 RT_NULL
 * @err           错误信息缓冲（可选）
 * 成功返回 socket fd（>=0），失败返回 -1 并填 err。
 */
int ws_connect(const char *host, int port, const char *path_query,
               const char *extra_headers, char *err, int err_len);

/*
 * 发送一帧。注意：payload 缓冲会被就地掩码修改（客户端掩码要求），
 * 调用方无需保留原数据。
 * @opcode 0x1=text 0x2=binary（内部自动 FIN=1 + 掩码）
 */
int ws_send(int fd, int opcode, uint8_t *payload, int len);

/*
 * 接收一帧数据（自动处理 ping→pong；内部有 SO_RCVTIMEO 超时）。
 * @payload       输出缓冲（>=payload_cap）
 * @payload_cap   缓冲容量
 * @timeout_ms    保留参数（超时由连接时的 SO_RCVTIMEO 决定）
 * @opcode_out    返回帧类型（0x1/0x2/0x0）
 * 返回 payload 长度；-1 超时/断开；-2 收到 CLOSE；-3 帧超容量。
 */
int ws_recv_frame(int fd, uint8_t *payload, int payload_cap,
                  int timeout_ms, int *opcode_out);

/* 关闭连接并释放 socket */
void ws_close(int fd);

#endif /* VOICE_WS_CLIENT_H */
