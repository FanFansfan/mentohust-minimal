/* -*- Mode: C; tab-width: 4; -*- */
/*
* Copyright (C) 2009, HustMoon Studio
*
* 文件名称：mentohust.c
* 摘	要：MentoHUST主函数
* 作	者：HustMoon@BYHH
* 邮	箱：www.ehust@gmail.com
*/
#include "config.h"

#include "myconfig.h"
#include "mystate.h"
#include "myfunc.h"
#include "dlfunc.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>

extern pcap_t *hPcap;
extern volatile int state;
extern uint8_t *fillBuf;
extern const uint8_t *capBuf;
extern unsigned startMode, dhcpMode, maxFail;
extern uint8_t destMAC[];
extern int lockfd;
#ifndef NO_ARP
extern uint32_t rip, gateway;
extern uint8_t gateMAC[];
#endif

static void exit_handle(void);	/* 退出回调 */
static void sig_handle(int sig);	/* 信号回调 */
static void pcap_handle(uint8_t *user, const struct pcap_pkthdr *h, const uint8_t *buf);	/* pcap_loop回调 */
static void showRuijieMsg(const uint8_t *buf, unsigned bufLen);	/* 显示锐捷服务器提示信息 */
static void showCernetMsg(const uint8_t *buf);	/* 显示赛尔服务器提示信息 */

int main(int argc, char **argv)
{
	atexit(exit_handle);
	initConfig(argc, argv);
	signal(SIGALRM, sig_handle);	/* 定时器 */
	signal(SIGHUP, sig_handle);	 /* 注销时 */
	signal(SIGINT, sig_handle);	 /* Ctrl+C */
	signal(SIGQUIT, sig_handle);	/* Ctrl+\ */
	signal(SIGTSTP, sig_handle);	/* Ctrl+Z */
	signal(SIGTERM, sig_handle);	/* 被结束时 */
	if (dhcpMode == 3)	  /* 认证前DHCP */
		switchState(ID_DHCP);
	else
		switchState(ID_START);	/* 开始认证 */
	if (-1 == pcap_loop(hPcap, -1, pcap_handle, NULL)) { /* 开始捕获数据包 */
		printf("!! pcap_loop failed\n");
	}
	exit(EXIT_FAILURE);
}

static void exit_handle(void)
{
	if (state != ID_DISCONNECT)
		switchState(ID_DISCONNECT);
	if (hPcap != NULL)
		pcap_close(hPcap);
	if (fillBuf != NULL)
		free(fillBuf);
	if (lockfd > -1)
		close(lockfd);
#ifndef NO_DYLOAD
	free_libpcap();
#endif
	printf(">> exited\n");
}

static void sig_handle(int sig)
{
	if (sig == SIGALRM)	 /* 定时器 */
	{
		if (-1 == switchState(state))
		{
			pcap_breakloop(hPcap);
			printf("!! failed sending packets, please check your network connection\n");
			exit(EXIT_FAILURE);
		}
	}
	else	/* 退出 */
	{
		pcap_breakloop(hPcap);
		exit(EXIT_SUCCESS);
	}
}

static void pcap_handle(uint8_t *user, const struct pcap_pkthdr *h, const uint8_t *buf)
{
	static unsigned failCount = 0;
#ifndef NO_ARP
	if (buf[0x0c]==0x88 && buf[0x0d]==0x8e) {
#endif
		if (memcmp(destMAC, buf+6, 6)!=0 && startMode>2)	/* 服务器MAC地址不符 */
			return;
		capBuf = buf;
		if (buf[0x0F]==0x00 && buf[0x12]==0x01 && buf[0x16]==0x01) {	/* 验证用户名 */
			if (startMode < 3) {
				memcpy(destMAC, buf+6, 6);
				printf("** authentication server MAC: \t%s\n", formatHex(destMAC, 6));
				startMode += 3;	/* 标记为已获取 */
			}
			if (startMode==3 && memcmp(buf+0x17, "User name", 9)==0)	/* 塞尔 */
				startMode = 5;
			switchState(ID_IDENTITY);
		}
		else if (buf[0x0F]==0x00 && buf[0x12]==0x01 && buf[0x16]==0x04)	/* 验证密码 */
			switchState(ID_CHALLENGE);
		else if (buf[0x0F]==0x00 && buf[0x12]==0x03) {	/* 认证成功 */
			printf(">> successfully authenticated\n");
			failCount = 0;
			if (!(startMode%3 == 2)) {
				getEchoKey(buf);
				showRuijieMsg(buf, h->caplen);
			}
			if (dhcpMode==1 || dhcpMode==2)	/* 二次认证第一次或者认证后 */
				switchState(ID_DHCP);
			else if (startMode%3 == 2)
				switchState(ID_WAITECHO);
			else
				switchState(ID_ECHO);
		}
		else if (buf[0x0F]==0x00 && buf[0x12]==0x01 && buf[0x16]==0x02)	/* 显示赛尔提示信息 */
			showCernetMsg(buf);
		else if (buf[0x0F] == 0x05)	/* (赛尔)响应在线 */
			switchState(ID_ECHO);
		else if (buf[0x0F]==0x00 && buf[0x12]==0x04) {  /* 认证失败或被踢下线 */
			if (state==ID_WAITECHO || state==ID_ECHO) {
				printf(">> authentication offline, reconnecting\n");
				switchState(ID_START);
			}
			else if (buf[0x1b]!=0 || startMode%3==2) {
				printf(">> authentication failed\n");
				if (startMode%3 != 2)
					showRuijieMsg(buf, h->caplen);
				if (maxFail && ++failCount>=maxFail) {
					printf(">> authentication failed for %u times, exiting\n", maxFail);
					exit(EXIT_SUCCESS);
				}
				restart();
			}
			else
				switchState(ID_START);
		}
#ifndef NO_ARP
	} else if (gateMAC[0]!=0xFE && buf[0x0c]==0x08 && buf[0x0d]==0x06) {
		if (*(uint32_t *)(buf+0x1c) == gateway) {
			char str[50];
			if (gateMAC[0] == 0xFF) {
				memcpy(gateMAC, buf+0x16, 6);
				printf("** gateway MAC address: \t%s\n", formatHex(gateMAC, 6));
				sprintf(str, "arp -s %s %s", formatIP(gateway), formatHex(gateMAC, 6));
				system(str);
			} else if (buf[0x15]==0x02 && memcmp(&rip, buf+0x26, 4)==0
				&& memcmp(gateMAC, buf+0x16, 6)!=0) {
				printf("** ARP spoofing: \t%s\n", formatHex(buf+0x16, 6));
			}
		}
	}
#endif
}

static void showRuijieMsg(const uint8_t *buf, unsigned bufLen)
{
	char *serverMsg;
	int length = buf[0x1b];
	if (length > 0) {
		for (serverMsg=(char *)(buf+0x1c); *serverMsg=='\r'||*serverMsg=='\n'; serverMsg++,length--);	/* 跳过开头的换行符 */
		if (strlen(serverMsg) < length)
			length = strlen(serverMsg);
		if (length>0 && (serverMsg=gbk2utf(serverMsg, length))!=NULL) {
			if (strlen(serverMsg)) {
				printf("$$ system notice: \t%s\n", serverMsg);
			}
			free(serverMsg);
		}
	}
	if ((length=0x1c+buf[0x1b]+0x69+39) < bufLen) {
		serverMsg=(char *)(buf+length);
		if (buf[length-1]-2 > bufLen-length)
			length = bufLen - length;
		else
			length = buf[length-1]-2;
		for (; *serverMsg=='\r'||*serverMsg=='\n'; serverMsg++,length--);
		if (length>0 && (serverMsg=gbk2utf(serverMsg, length))!=NULL) {
			if (strlen(serverMsg)) {
				printf("$$ billing notice:\t%s\n", serverMsg);
			}
			free(serverMsg);
		}
	}
}

static void showCernetMsg(const uint8_t *buf)
{
	char *serverMsg = (char *)(buf+0x17);
	int length = ntohs(*(uint16_t *)(buf+0x14)) - 5;
	if (strlen(serverMsg) < length)
		length = strlen(serverMsg);
	if (length>0 && (serverMsg=gbk2utf(serverMsg, length))!=NULL)
	{
		printf("$$ system notice:\t%s\n", serverMsg);
		free(serverMsg);
	}
}
