#ifndef CAN_ETH_H
#define CAN_ETH_H

#include <arpa/inet.h>
#include <menge.h>
#include <boolean.h>
#include "can_io.h"

#define FD_POS_UDP        0x01
#define FD_POS_TCP_SERVER 0x02
#define FD_POS_TCP_CLIENT 0x03
#define FD_POS_ENDE       0x04

typedef struct {
   int ClientSock;
   struct sockaddr_in ClientAddr;
} ClientInfo;

#define ClientInfoSetClientSock(Data, Sock) (Data)->ClientSock=Sock
#define ClientInfoSetClientAddr(Data, Addr) (Data)->ClientAddr=Addr

#define ClientInfoGetClientSock(Data) (Data)->ClientSock
#define ClientInfoGetClientAddr(Data) (Data)->ClientAddr

typedef struct {
   BOOL Verbosity;
   BOOL SendUdpBc;
   BOOL UdpConnected;
   char BcIp[20];
   int OutsideUdpSock;
   int OutsideTcpSock;
   struct sockaddr_in ClientAddr;
   Menge *Clients;
   MengeIterator *ClientIter;
   MengeIterator *FdSearchIter;
   int FdPos;
   ClientInfo *FdSearchClient;
} CanEthStruct;

#define CanEthSetVerbose(Data, Verbose)       (Data)->Verbosity=Verbose
#define CanEthSetSendUdpBc(Data, SendBc)      (Data)->SendUdpBc=SendBc
#define CanEthSetUdpConnected(Data, IsCon)    (Data)->UdpConnected=IsCon
#define CanEthSetBcIp(Data, IpAddr)           strcpy((Data)->BcIp,IpAddr)
#define CanEthSetOutsideUdpSock(Data, Sock)   (Data)->OutsideUdpSock=Sock
#define CanEthSetOutsideTcpSock(Data, Sock)   (Data)->OutsideTcpSock=Sock
#define CanEthSetClients(Data, Client)        (Data)->Clients=Client
#define CanEthSetClientIter(Data, Iter)       (Data)->ClientIter=Iter
#define CanEthSetFdSearchIter(Data, Iter)     (Data)->FdSearchIter=Iter
#define CanEthSetFdPos(Data, Pos)             (Data)->FdPos=Pos
#define CanEthSetFdSearchClient(Data, Client) (Data)->FdSearchClient=Client

#define CanEthGetVerbose(Data)        (Data)->Verbosity
#define CanEthGetSendUdpBc(Data)      (Data)->SendUdpBc
#define CanEthGetUdpConnected(Data)   (Data)->UdpConnected
#define CanEthGetBcIp(Data)           (Data)->BcIp
#define CanEthGetOutsideUdpSock(Data) (Data)->OutsideUdpSock
#define CanEthGetOutsideTcpSock(Data) (Data)->OutsideTcpSock
#define CanEthGetClientAddr(Data)     (Data)->ClientAddr
#define CanEthGetClients(Data)        (Data)->Clients
#define CanEthGetClientIter(Data)     (Data)->ClientIter
#define CanEthGetFdSearchIter(Data)   (Data)->FdSearchIter
#define CanEthGetFdPos(Data)          (Data)->FdPos
#define CanEthGetFdSearchClient(Data) (Data)->FdSearchClient

IoFktStruct *CanEthInit(BOOL Verbose, BOOL SendBc, char *BcAddr);
void CanEthExit(IoFktStruct *Data);

#endif
