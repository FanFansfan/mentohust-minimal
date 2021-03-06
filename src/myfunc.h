/*
* Copyright (C) 2009, HustMoon Studio
*
* 文件名称：myfunc.h
* 摘	要：认证相关算法及方法
* 作	者：HustMoon@BYHH
*/
#ifndef HUSTMOON_MYFUNC_H
#define HUSTMOON_MYFUNC_H

#include <sys/types.h>
#include <stdint.h>

char *gbk2utf(char *src, size_t srclen);	/* GBK转UTF－8 */
char *formatIP(uint32_t ip);	/* 格式化IP */
char *formatHex(const void *buf, int length);	/* 格式化成十六进制形式 */
void printSuConfig(const char *SuConfig);   /* 显示锐捷配置文件 */
void newBuffer();	/* 检测数据文件有效性并分配内存 */
int fillHeader();	/* 填充网络地址及校验值部分 */
void fillStartPacket();	/* 填充Start包 */
void fillMd5Packet(const uint8_t *md5Seed);	/* 填充Md5包 */
void fillEchoPacket(uint8_t *buf);	/* 填充Echo包 */
void getEchoKey(const uint8_t *capBuf);	/* 获取EchoKey */
uint8_t *checkPass(uint8_t id, const uint8_t *md5Seed, int seedLen);	/* 计算密码的md5 */
void fillCernetAddr(uint8_t *buf);	/* 填充赛尔网络地址 */
int isOnline();	/* ping主机判断是否掉线 */

#endif

