#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <boolean.h>
#include <bytestream.h>
#include <cs2parse.h>
#include <write_cs2.h>
#include <fsm.h>
#include <mr_ipc.h>
#include <config.h>
#include "zentrale.h"
#include "lok.h"
#include "lokstatus.h"
#include "magstat.h"
#include "gbsstat.h"
#include "fsstat.h"

#define PERIODIC_NAME_QUERY     "query_member"
#define PERIODIC_NAME_END_QUERY "end_query_member"
#define PERIODIC_NAME_POLLMS2   "poll_ms2"
#define PERIODIC_NAME_PING      "ping"
#define PERIODIC_NAME_TO_CS2    "timeout_cs2"
#define PERIODIC_NAME_TO_MS2    "timeout_ms2"
#define PERIODIC_NAME_LOKNAME   "query_lokname"
#define PERIODIC_NAME_LOKINFO   "query_lokinfo"
#define PERIODIC_NAME_TO_NORMAL "timeout_normal"

#define PERIODIC_INTERVALL_QUERY    10
#define PERIODIC_INTERVALL_POLLMS2  60
#define PERIODIC_INTERVALL_PING     10
#define PERIODIC_INTERVALL_LOKNAME   1
#define PERIODIC_INTERVALL_LOKINFO   1
#define PERIODIC_INTERVALL_TIMEOUT   5
#define PERIODIC_INTERVALL_QPERIOD   1
#define STATE_TIMEOUT 5

#define PARAGRAPH_UNDEFINED 0
#define PARAGRAPH_LOK       1
#define PARAGRAPH_NUMLOKS   2

#define NUM_STATES 27
#define STATE_WAIT_FOR_MS2                0
#define STATE_WAIT_LOKNAME_CFG_HDR        1
#define STATE_WAIT_LOKNAME_CFG_DATA       2
#define STATE_WAIT_LOKINFO_CFG_HDR        3
#define STATE_WAIT_LOKINFO_CFG_DATA       4
#define STATE_WAIT_CFG_DATA               5
#define STATE_WAIT_CS2                    6
#define STATE_NORMAL                      7
#define STATE_GET_LOK_CS2_CFG_DATA        8
#define STATE_WAIT_LOK_CS2_CFG_HDR        9
#define STATE_WAIT_LOK_CS2_CFG_DATA      10
#define STATE_WAIT_MAG_CS2_CFG_HDR       11
#define STATE_WAIT_MAG_CS2_CFG_DATA      12
#define STATE_WAIT_FS_CS2_CFG_HDR        13
#define STATE_WAIT_FS_CS2_CFG_DATA       14
#define STATE_WAIT_GBS_CS2_CFG_HDR       15
#define STATE_WAIT_GBS_CS2_CFG_DATA      16
#define STATE_WAIT_GPG_CS2_CFG_HDR       17
#define STATE_WAIT_GPG_CS2_CFG_DATA      18
#define STATE_WAIT_LOK_CVR_CFG_HDR       19
#define STATE_WAIT_LOK_CVR_CFG_DATA      20
#define STATE_WAIT_MAG_CVR_CFG_HDR       21
#define STATE_WAIT_MAG_CVR_CFG_DATA      22
#define STATE_WAIT_GBS_CVR_CFG_HDR       23
#define STATE_WAIT_GBS_CVR_CFG_DATA      24
#define STATE_WAIT_FS_CVR_CFG_HDR        25
#define STATE_WAIT_FS_CVR_CFG_DATA       26


#define NUM_SIGNALS 21


typedef struct {
   int Length;
   unsigned char Data[8];
} S88SystemConfig;


static S88SystemConfig S88Bootldr[3] = {
   { 5, { 0x53, 0x38, 0x38, 0x00, 0xE4, 0x00, 0x00, 0x00 } },
   { 5, { 0x53, 0x38, 0x38, 0x00, 0x11, 0x00, 0x00, 0x00 } },
   { 7, { 0x53, 0x38, 0x38, 0x00, 0x0C, 0x00, 0x00, 0x00 } } };


static void QueryMembers(ZentraleStruct *Data)
{  MrIpcCmdType Cmd;

   if (!ZentraleGetIsInPoll(Data))
   {
      CanMemberDelAllInvalid(ZentraleGetCanMember(Data));
      CanMemberMarkAllInvalid(ZentraleGetCanMember(Data));
      ZentraleSetIsInPoll(Data, TRUE);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_END_QUERY);
   }
   if (ZentraleGetVerbose(Data))
      puts("FSM: query members");
   MrIpcInit(&Cmd);
   MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetCanResponse(&Cmd, 0);
   MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
   MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_PING);
   MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
   MrIpcCmdSetRequestMember(&Cmd);
   MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
}

static int PeriodicEndQueryMembers(void *PrivData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)PrivData;
   ZentraleSetIsInPoll(Data, FALSE);
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_END_QUERY);
   return(STATE_NO_CHANGE);
}

static int PeriodicQueryMembers(void *PrivData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)PrivData;
   QueryMembers(Data);
   return(STATE_NO_CHANGE);
}

static int PeriodicPollMs2(void *PrivData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;

   Data = (ZentraleStruct *)PrivData;
   if (ZentraleGetVerbose(Data))
      puts("FSM: periodic task poll MS2");
   LokMarkAllDeleted(ZentraleGetLoks(Data));
   ZentraleSetActualIndex(Data, 0);
   MrIpcInit(&Cmd);
   MrIpcSetSenderSocket(&Cmd, ZentraleGetClientSock(Data));
   MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetCanResponse(&Cmd, 0);
   MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
   MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
   MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
   MrIpcCmdSetReqestLocname(&Cmd, ZentraleGetActualIndex(Data), 2);
   MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2);
   CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   return(STATE_NO_CHANGE);
}

static int PeriodicPing(void *PrivData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;

   Data = (ZentraleStruct *)PrivData;
   MrIpcInit(&Cmd);
   MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetCanResponse(&Cmd, 1);
   MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
   MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_PING);
   MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
   MrIpcCmdSetMember(&Cmd, ZentraleGetUid(Data), 0x100, MR_CS2_DEVID_CS2);
   MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
   return(STATE_NO_CHANGE);
}

static int PeriodicQueryLokname(void *PrivData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;

   Data = (ZentraleStruct *)PrivData;
   if (ZentraleGetActualIndex(Data) < ZentraleGetNumLoks(Data))
   {
      if (ZentraleGetVerbose(Data))
         printf("request lokname %d\n", ZentraleGetActualIndex(Data));
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, ZentraleGetClientSock(Data));
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetReqestLocname(&Cmd, ZentraleGetActualIndex(Data), 2);
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      ZentraleSetActualIndex(Data, 0);
      if (ZentraleGetVerbose(Data))
         printf("request lokinfo %d >%s< from %d\n",
                ZentraleGetActualIndex(Data),
                ZentraleGetLokNamenNr(Data, ZentraleGetActualIndex(Data)),
                ZentraleGetNumLoks(Data));
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, ZentraleGetClientSock(Data));
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetReqestLocinfo(&Cmd,
                               ZentraleGetLokNamenNr(Data,
                                                     ZentraleGetActualIndex(Data)));
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKNAME);
   return(STATE_NO_CHANGE);
}

static int PeriodicQueryLokinfo(void *PrivData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;

   Data = (ZentraleStruct *)PrivData;
   if (ZentraleGetActualIndex(Data) < ZentraleGetNumLoks(Data))
   {
      if (ZentraleGetVerbose(Data))
         printf("request lokinfo %d >%s< from %d\n",
                ZentraleGetActualIndex(Data),
                ZentraleGetLokNamenNr(Data,
                                      ZentraleGetActualIndex(Data)),
                ZentraleGetNumLoks(Data));
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, ZentraleGetClientSock(Data));
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetReqestLocinfo(&Cmd,
                               ZentraleGetLokNamenNr(Data,
                                                     ZentraleGetActualIndex(Data)));
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_PERIODIC) != 0)
      {
         if (ZentraleGetVerbose(Data))
            puts("enable periodic poll of MS2");
         CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2);
      }
      else
      {
         if (ZentraleGetVerbose(Data))
            puts("disable periodic poll of MS2");
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2);
      }
   }
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKINFO);
   return(STATE_NO_CHANGE);
}

static int PeriodicTimeoutCs2(void *PrivData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;

   Data = (ZentraleStruct *)PrivData;
   MrIpcInit(&Cmd);
   MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetCanResponse(&Cmd, 0);
   MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
   MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
   MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_0);
   MrIpcCmdSetQuery(&Cmd, MR_CS2_CFG_DLC, MR_CS2_CFG_LOKS);
   MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
   LokMarkAllDeleted(ZentraleGetLoks(Data));
   if (ZentraleGetVerbose(Data))
      printf("FSM: timer, new state %d\n",STATE_WAIT_LOK_CS2_CFG_HDR);
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_CS2);
   CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   return(STATE_WAIT_LOK_CS2_CFG_HDR);
}

static int PeriodicTimeoutWaitMS2(void *PrivData)
{
   return(STATE_WAIT_FOR_MS2);
}

static int PeriodicTimeoutWaitNormal(void *PrivData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)PrivData;
   if (ZentraleGetVerbose(Data))
      printf("FSM: timeout, new state %d\n",STATE_NORMAL);
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   LokMarkAllUndeleted(ZentraleGetLoks(Data));
   return(STATE_NORMAL);
}

static int HandleTimer(void *Priv, void *SignalData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   if (ZentraleGetVerbose(Data))
      puts("FSM: periodic task");
   return(CronDo(ZentraleGetCronJobs(Data)));
}

static int HandleMemberWaitMs2(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame, Cmd;
   unsigned long Uid;
   unsigned int Version, Type;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetMember(CmdFrame, &Uid, &Version, &Type);
   if (ZentraleGetVerbose(Data))
      printf("FSM: CAN member %lu, version %d, type 0x%x\n",
             Uid, Version, Type);
   if ((Type == MR_CS2_DEVID_MS2_1) || (Type == MR_CS2_DEVID_MS2_2))
   {
      LokMarkAllDeleted(ZentraleGetLoks(Data));
      ZentraleSetActualIndex(Data, 0);
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetReqestLocname(&Cmd, ZentraleGetActualIndex(Data), 2);
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      if (ZentraleGetVerbose(Data))
         printf("FSM: new state %d\n",STATE_WAIT_LOKNAME_CFG_HDR);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      return(STATE_WAIT_LOKNAME_CFG_HDR);
   }
   else
      return(STATE_NO_CHANGE);
}

static int HandleLokSpeed(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Addr;
   unsigned Speed;
   LokInfo *LokData;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetLocomotiveSpeed(CmdFrame, &Addr, &Speed);
   if (ZentraleGetVerbose(Data))
      printf("FSM: LOK Speed addr %lu, speed %d (response %d)\n",
             Addr, Speed, MrIpcGetCanResponse(CmdFrame));
   if (MrIpcGetCanResponse(CmdFrame))
   {
      LokData = LokSearch(ZentraleGetLoks(Data), Addr);
      if (LokData != (LokInfo *)NULL)
         LokInfoSetVelocity(LokData, Speed);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleLokDirection(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Addr;
   DirectionType Direction;
   LokInfo *LokData;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetLocomotiveDir(CmdFrame, &Addr, &Direction);
   if (ZentraleGetVerbose(Data))
      printf("FSM: LOK Direction addr %lu, speed %d (response %d)\n",
             Addr, (int)Direction, MrIpcGetCanResponse(CmdFrame));
   if (MrIpcGetCanResponse(CmdFrame))
   {
      LokData = LokSearch(ZentraleGetLoks(Data), Addr);
      if (LokData != (LokInfo *)NULL)
         LokInfoSetRichtung(LokData, (int)Direction);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleLokFunction(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Addr;
   unsigned Function;
   SwitchType Switch;
   LokInfo *LokData;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetLocomotiveFkt(CmdFrame, &Addr, &Function, &Switch);
   if (ZentraleGetVerbose(Data))
      printf("FSM: LOK Function addr %lu, function %d, value %d (response %d)\n",
             Addr, Function, (int)Switch, MrIpcGetCanResponse(CmdFrame));
   if (MrIpcGetCanResponse(CmdFrame))
   {
      LokData = LokSearch(ZentraleGetLoks(Data), Addr);
      if (LokData != (LokInfo *)NULL)
         LokInfoSetFktWert(LokData, Function, (int)Switch);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleFileRequest(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame, Cmd;
   char Name[9], *Dateiname, FullPath[255], *LokomotiveDaten;
   int i, j, FileLength;
   unsigned Hash;
   struct stat attribut;
   FILE *LokomotiveDatei;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetQuery(CmdFrame, Name);
   Name[8] = '\0';
   if (ZentraleGetVerbose(Data))
      printf("FSM: request %s\n", Name);
   if (strncmp(MR_CS2_CFG_LOCINFO, Name, strlen(MR_CS2_CFG_LOCINFO)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_LOCNAMES, Name, strlen(MR_CS2_CFG_LOCNAMES)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_MAGINFO, Name, strlen(MR_CS2_CFG_MAGINFO)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_LOCDB, Name, strlen(MR_CS2_CFG_LOCDB)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_LANGVER, Name, strlen(MR_CS2_CFG_LANGVER)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_LANG, Name, strlen(MR_CS2_CFG_LANG)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_LDBVER, Name, strlen(MR_CS2_CFG_LDBVER)) == 0)
      Dateiname = (char *)NULL;
   else if (strncmp(MR_CS2_CFG_LOK_STAT, Name, strlen(MR_CS2_CFG_LOK_STAT)) == 0)
   {
      LokStatusSaveLokomotiveSr2(ZentraleGetLoks(Data));
      Dateiname = CS2_FILE_STRING_STATUS_LOKOMOTIVE;
   }
   else if (strncmp(MR_CS2_CFG_LOKS, Name, strlen(MR_CS2_CFG_LOKS)) == 0)
      Dateiname = CS2_FILE_STRING_LOKOMOTIVE;
   else if (strncmp(MR_CS2_CFG_MAGS, Name, strlen(MR_CS2_CFG_MAGS)) == 0)
      Dateiname = CS2_FILE_STRING_MAGNETARTIKEL;
   else if (strncmp(MR_CS2_CFG_GBS_PAGE, Name, strlen(MR_CS2_CFG_GBS_PAGE)) == 0)
   {
      if (atoi(Name + strlen(MR_CS2_CFG_GBS_PAGE)) <
          GleisbildGetNumPages(ZentraleGetGleisbild(Data)))
      {
         if (ZentraleGetNrGleisPages(Data,
                                     atoi(Name + strlen(MR_CS2_CFG_GBS_PAGE))) != (GleisbildPageStruct *)NULL)
         {
            Dateiname = GleisbildPageGetName(ZentraleGetNrGleisPages(Data,
                                                                     atoi(Name + strlen(MR_CS2_CFG_GBS_PAGE))));
         }
         else
            Dateiname = (char *)NULL;
      }
      else
         Dateiname = (char *)NULL;
   }
   else if (strncmp(MR_CS2_CFG_GBS_STAT, Name, strlen(MR_CS2_CFG_GBS_STAT)) == 0)
   {
      GbsStatSaveGbsStatSr2(ZentraleGetGleisbild(Data));
      Dateiname = CS2_FILE_STRING_STATUS_GLEISBILD;
   }
   else if (strncmp(MR_CS2_CFG_GBS, Name, strlen(MR_CS2_CFG_GBS)) == 0)
      Dateiname = CS2_FILE_STRING_GLEISBILD;
   else if (strncmp(MR_CS2_CFG_FS, Name, strlen(MR_CS2_CFG_FS)) == 0)
      Dateiname = CS2_FILE_STRING_FAHRSTRASSE;
   else if (strncmp(MR_CS2_CFG_MAG_STAT, Name, strlen(MR_CS2_CFG_MAG_STAT)) == 0)
   {
      MagStatusSaveMagStatusSr2(ZentraleGetMagnetartikel(Data));
      Dateiname = CS2_FILE_STRING_STATUS_MAGNETARTIKEL;
   }
   else if (strncmp(MR_CS2_CFG_FS_STAT, Name, strlen(MR_CS2_CFG_FS_STAT)) == 0)
   {
      FsStatSaveFsStatSr2(ZentraleGetFahrstrasse(Data));
      Dateiname = CS2_FILE_STRING_STATUS_FAHRSTRASSE;
   }
   else
      Dateiname = (char *)NULL;
   if (Dateiname != (char *)NULL)
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: request file %s\n", Dateiname);
      strcpy(FullPath, ZentraleGetLocPath(Data));
      if (FullPath[strlen(FullPath)] != '/')
         strcat(FullPath, "/");
      strcat(FullPath, Dateiname);
      LokomotiveDatei = fopen(FullPath, "r");
      if (LokomotiveDatei != (FILE *)NULL)
      {
         if (stat(FullPath, &attribut) == 0)
         {
            LokomotiveDaten = (char *)malloc(attribut.st_size);
            if (LokomotiveDaten != (char *)NULL)
            {
               FileLength = fread(LokomotiveDaten, 1, attribut.st_size,
                                  LokomotiveDatei);
               if (FileLength > 0)
               {
                  ZFileInit(ZentraleGetPackedCs2File(Data), LokomotiveDaten,
                            FileLength);
                  if (ZFileCompress(ZentraleGetPackedCs2File(Data)))
                  {
                     MrIpcSetCanResponse(CmdFrame, 1);
                     Hash = MrCs2CalcHash(ZentraleGetUid(Data));
                     MrIpcSetCanHash(CmdFrame, MrCs2CalcHash(ZentraleGetUid(Data)));
                     MrIpcSetReceiverSocket(CmdFrame,
                                            MrIpcGetSenderSocket(CmdFrame));
                     MrIpcSetSenderSocket(CmdFrame, MR_IPC_SOCKET_ALL);
                     MrIpcSend(ZentraleGetClientSock(Data), CmdFrame);
                     MrIpcSetCanHash(CmdFrame, Hash);
                     MrIpcInit(&Cmd);
                     MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
                     MrIpcSetReceiverSocket(&Cmd,
                                            MrIpcGetReceiverSocket(CmdFrame));
                     MrIpcSetCanResponse(&Cmd, 0);
                     MrIpcSetCanHash(&Cmd, Hash);
                     MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CFGDAT_STREAM);
                     MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_0);
                     MrIpcCmdSetCfgHeader(&Cmd,
                         ZFileGetLength(ZentraleGetPackedCs2File(Data)),
                         ZFileGetCrc(ZentraleGetPackedCs2File(Data)));
                     MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
                     i = 0;
                     while (i < ZFileGetFrameLength(ZentraleGetPackedCs2File(Data)))
                     {
                        MrIpcInit(&Cmd);
                        MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
                        MrIpcSetReceiverSocket(&Cmd,
                                               MrIpcGetReceiverSocket(CmdFrame));
                        MrIpcSetCanResponse(&Cmd, 0);
                        MrIpcSetCanHash(&Cmd, Hash);
                        MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CFGDAT_STREAM);
                        MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
                        j = 0;
                        while (j < 8)
                        {
                           Name[j] = ZFileGetBuffer(ZentraleGetPackedCs2File(Data))[i + j];
                           j++;
                        }
                        MrIpcCmdSetCfgData(&Cmd, Name);
                        MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
                        i += 8;
                     }
                  }
                  else if (ZentraleGetVerbose(Data))
                     printf("FSM: error in compress file %s\n", Dateiname);
                  ZFileExit(ZentraleGetPackedCs2File(Data));
               }
               else if (ZentraleGetVerbose(Data))
                  printf("FSM: error in read file %s\n", Dateiname);
               free(LokomotiveDaten);
            }
            else if (ZentraleGetVerbose(Data))
               printf("FSM: error in allocate file buffer for %s\n", Dateiname);
            fclose(LokomotiveDatei);
         }
         else if (ZentraleGetVerbose(Data))
            printf("FSM: error in get file size %s\n", FullPath);
      }
      else if (ZentraleGetVerbose(Data))
         printf("FSM: error in open file %s\n", FullPath);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static BOOL DoCfgHeader(ZentraleStruct *Data, MrIpcCmdType *CmdFrame,
                        BOOL IsCompressed)
{  unsigned long Length;
   unsigned int Crc;

   MrIpcCmdGetCfgHeader(CmdFrame, &Length, &Crc);
   if (ZentraleGetVerbose(Data))
      printf("DoCfgHeader: CFG data length %lu, crc %d\n", Length, Crc);
   if (!Cs2CfgDataStart(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame),
                        Length, IsCompressed))
   {
      if (ZentraleGetVerbose(Data))
         puts("DoCfgHeader: error");
      return(FALSE);
   }
   else
   {
      return(TRUE);
   }
}

static int HandleCfgHeaderUncompressed(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   DoCfgHeader(Data, CmdFrame, FALSE);
   return(STATE_NO_CHANGE);
}

static int HandleCfgHeaderCompressed(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   DoCfgHeader(Data, CmdFrame, TRUE);
   return(STATE_NO_CHANGE);
}

static int HandleLoknameWaitCfgHeader(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   if (DoCfgHeader(Data, CmdFrame, FALSE))
   {
      if (ZentraleGetVerbose(Data))
      {
         puts("HandleLoknameWaitCfgHeader: continue");
         printf("FSM: new state %d\n",STATE_WAIT_LOKNAME_CFG_DATA);
      }
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      return(STATE_WAIT_LOKNAME_CFG_DATA);
   }
   else
   {
      if (ZentraleGetVerbose(Data))
      {
         puts("HandleLoknameWaitCfgHeader: error");
         printf("FSM: new state %d\n",STATE_NORMAL);
      }
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      return(STATE_NORMAL);
   }
}

static int HandleLokinfoWaitCfgHeader(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   if (DoCfgHeader(Data, CmdFrame, FALSE))
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      if (ZentraleGetVerbose(Data))
         printf("FSM: new state %d\n",STATE_WAIT_LOKINFO_CFG_DATA);
      return(STATE_WAIT_LOKINFO_CFG_DATA);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      if (ZentraleGetVerbose(Data))
         printf("FSM: new state %d\n",STATE_NORMAL);
      return(STATE_NORMAL);
   }
}

static int HandleCfgHeaderProxy(void *Priv, void *SignalData,
                                int NewStateForContinue, int NewStateForStay)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   if (DoCfgHeader(Data, CmdFrame, TRUE))
   {
      return(NewStateForContinue);
   }
   else
   {
      return(NewStateForStay);
   }
}

static int HandleGetLokCfgHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_GET_LOK_CS2_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleLokCfgHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_LOK_CS2_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleMagCfgHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_MAG_CS2_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleFsCfgHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_FS_CS2_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleGbsCfgHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_GBS_CS2_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleGpgCfgHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_GPG_CS2_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleLokCvrHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_LOK_CVR_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleMagCvrHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_MAG_CVR_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleGbsCvrHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_GBS_CVR_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleFsCvrHeaderProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleCfgHeaderProxy(Priv, SignalData,
                                   STATE_WAIT_FS_CVR_CFG_DATA,
                                   STATE_NORMAL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static BOOL HandleCfgLoknamen(ZentraleStruct *Data, MrIpcCmdType *CmdFrame)
{  Cs2parser *LokParser;
   int LineInfo, Paragraph;

   Paragraph = PARAGRAPH_UNDEFINED;
   if (ZentraleGetVerbose(Data))
      printf("FSM: evaluate lokname cfg\n%s",
             Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
   Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame))[Cs2CfgDataGetLength(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame))] = '\0';
   LokParser = Cs2pCreate();
   Cs2pInit(LokParser, PARSER_TYPE_LOKNAMEN,
            Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
            Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
   Cs2pSetVerbose(LokParser, FALSE);
   do {
      LineInfo = Cs2pParse(LokParser);
      switch (LineInfo)
      {
         case PARSER_ERROR:
            if (ZentraleGetVerbose(Data))
               puts("ERROR in lok cfg");
            break;
         case PARSER_EOF:
            if (ZentraleGetVerbose(Data))
               puts("end of lok cfg");
            break;
         case PARSER_PARAGRAPH:
            if (ZentraleGetVerbose(Data))
               printf("new paragraph %s in lok cfg\n",
                      Cs2pGetName(LokParser));
            switch (Cs2pGetSubType(LokParser))
            {
               case PARSER_PARAGRAPH_LOK:
                  if (ZentraleGetVerbose(Data))
                     puts("lok paragraph in lok cfg");
                  Paragraph = PARAGRAPH_LOK;
                  break;
               case PARSER_PARAGRAPH_NUMLOKS:
                  if (ZentraleGetVerbose(Data))
                     puts("numloks paragraph in lok cfg");
                  Paragraph = PARAGRAPH_NUMLOKS;
                  break;
            }
            break;
         case PARSER_VALUE:
            if (ZentraleGetVerbose(Data))
               printf("new value %s=%s in lok cfg\n",
                      Cs2pGetName(LokParser), Cs2pGetValue(LokParser));
            switch (Cs2pGetSubType(LokParser))
            {
               case PARSER_VALUE_NAME:
                  if (ZentraleGetVerbose(Data))
                     printf("lok name %d in lok cfg\n", ZentraleGetActualIndex(Data));
                  if (Paragraph == PARAGRAPH_LOK &&
                      ZentraleGetActualIndex(Data) < ZentraleGetNumLoks(Data))
                  {
                     ZentraleSetLokNamenNr(Data,
                                           ZentraleGetActualIndex(Data),
                                           Cs2pGetValue(LokParser));
                     ZentraleSetActualIndex(Data, ZentraleGetActualIndex(Data) + 1);
                  }
                  break;
               case PARSER_VALUE_WERT:
                  if (ZentraleGetVerbose(Data))
                     puts("number of loks in lok cfg");
                  if (Paragraph == PARAGRAPH_NUMLOKS)
                  {
                     ZentraleSetNumLoks(Data, atoi(Cs2pGetValue(LokParser)));
                     if (ZentraleGetVerbose(Data))
                        printf("number of loks in lok cfg is %d\n",
                               ZentraleGetNumLoks(Data));
                     if (ZentraleGetMaxLoks(Data) < ZentraleGetNumLoks(Data))
                     {
                        ZentraleSetMaxLoks(Data, ZentraleGetNumLoks(Data));
                        ZentraleSetLokNamen(Data, realloc(ZentraleGetLokNamen(Data),
                                                          ZentraleGetNumLoks(Data) * sizeof(ZentraleLokName)));
                        if (ZentraleGetVerbose(Data))
                           printf("new number of loks in lok cfg is %d\n",
                                  ZentraleGetNumLoks(Data));
                     }
                  }
                  break;
            }
            break;
      }
   } while (LineInfo != PARSER_EOF);
   Cs2pDestroy(LokParser);
   if (ZentraleGetActualIndex(Data) < ZentraleGetNumLoks(Data))
   {
      return(FALSE);
   }
   else
   {
      LokMarkAllDeleted(ZentraleGetLoks(Data));
      return(TRUE);
   }
}

static BOOL HandleCfgLokinfo(ZentraleStruct *Data, MrIpcCmdType *CmdFrame)
{  Cs2parser *LokParser;
   int LineInfo, FktIndex;

   if (ZentraleGetVerbose(Data))
      printf("FSM: LOKINFO CFG data, aktuell %lu von %lu\n",
             Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
             Cs2CfgDataGetLength(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
   memset(ZentraleGetActualLok(Data), 0, sizeof(LokInfo));
   FktIndex = -1;
   Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame))[Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame))] = '\0';
   LokParser = Cs2pCreate();
   Cs2pInit(LokParser, PARSER_TYPE_LOKINFO,
            Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
            Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
   Cs2pSetVerbose(LokParser, FALSE);
   do {
      LineInfo = Cs2pParse(LokParser);
      switch (LineInfo)
      {
         case PARSER_ERROR:
            if (ZentraleGetVerbose(Data))
               puts("ERROR in lok info cfg");
            break;
         case PARSER_EOF:
            if (ZentraleGetVerbose(Data))
               puts("end of lok info cfg");
            break;
         case PARSER_PARAGRAPH:
            if (ZentraleGetVerbose(Data))
               printf("new paragraph %s in lok info cfg\n",
                      Cs2pGetName(LokParser));
            switch (Cs2pGetSubType(LokParser))
            {
               case PARSER_PARAGRAPH_LOKOMOTIVE:
                  if (ZentraleGetVerbose(Data))
                     puts("lokomotive paragraph in lok cfg");
                  break;
            }
            break;
         case PARSER_VALUE:
            if (ZentraleGetVerbose(Data))
               printf("new value %s=%s in lok cfg\n",
                      Cs2pGetName(LokParser), Cs2pGetValue(LokParser));
            switch (Cs2pGetSubType(LokParser))
            {
               case PARSER_VALUE_LOK:
                  if (ZentraleGetVerbose(Data))
                     puts("neuer lok Eintrag");
                  break;
               case PARSER_VALUE_UID:
                  if (ZentraleGetVerbose(Data))
                     puts("lok uid");
                  LokInfoSetUid(ZentraleGetActualLok(Data),
                                strtoul(Cs2pGetValue(LokParser),
                                        NULL, 0));
                  break;
               case PARSER_VALUE_NAME:
                  if (ZentraleGetVerbose(Data))
                     puts("lok name");
                  LokInfoSetName(ZentraleGetActualLok(Data),
                                 Cs2pGetValue(LokParser));
                  break;
               case PARSER_VALUE_ADRESSE:
                  if (ZentraleGetVerbose(Data))
                     puts("lok adresse");
                  LokInfoSetAdresse(ZentraleGetActualLok(Data),
                                    strtoul(Cs2pGetValue(LokParser),
                                            NULL, 0));
                  break;
               case PARSER_VALUE_TYP:
                  if (Cs2pGetLevel(LokParser) == 1)
                  {
                     if (ZentraleGetVerbose(Data))
                        puts("lok typ");
                     LokInfoSetTyp(ZentraleGetActualLok(Data),
                                   Cs2pGetValue(LokParser));
                  }
                  else if (Cs2pGetLevel(LokParser) == 2)
                  {
                     if (ZentraleGetVerbose(Data))
                        puts("lok function typ");
                     LokInfoSetFktTyp(ZentraleGetActualLok(Data), FktIndex,
                                      strtoul(Cs2pGetValue(LokParser),
                                              NULL, 0));
                  }
                  break;
               case PARSER_VALUE_MFXUID:
                  if (ZentraleGetVerbose(Data))
                     puts("lok mfxuid");
                  LokInfoSetMfxUid(ZentraleGetActualLok(Data),
                                   strtoul(Cs2pGetValue(LokParser),
                                           NULL, 0));
                  break;
               case PARSER_VALUE_SYMBOL:
                  if (ZentraleGetVerbose(Data))
                     puts("lok symbol");
                  LokInfoSetSymbol(ZentraleGetActualLok(Data),
                                   strtoul(Cs2pGetValue(LokParser),
                                           NULL, 0));
                  break;
               case PARSER_VALUE_AV:
                  if (ZentraleGetVerbose(Data))
                     puts("lok av");
                  LokInfoSetAv(ZentraleGetActualLok(Data),
                               strtoul(Cs2pGetValue(LokParser),
                                       NULL, 0));
                  break;
               case PARSER_VALUE_BV:
                  if (ZentraleGetVerbose(Data))
                     puts("lok bv");
                  LokInfoSetBv(ZentraleGetActualLok(Data),
                               strtoul(Cs2pGetValue(LokParser),
                                       NULL, 0));
                  break;
               case PARSER_VALUE_VOLUME:
                  if (ZentraleGetVerbose(Data))
                     puts("lok volume");
                  LokInfoSetVolume(ZentraleGetActualLok(Data),
                                   strtoul(Cs2pGetValue(LokParser),
                                           NULL, 0));
                  break;
               case PARSER_VALUE_VELOCITY:
                  if (ZentraleGetVerbose(Data))
                     puts("lok velocity");
                  LokInfoSetVelocity(ZentraleGetActualLok(Data),
                                     strtoul(Cs2pGetValue(LokParser),
                                             NULL, 0));
                  break;
               case PARSER_VALUE_RICHTUNG:
                  if (ZentraleGetVerbose(Data))
                     puts("lok richtung");
                  LokInfoSetRichtung(ZentraleGetActualLok(Data),
                                     strtoul(Cs2pGetValue(LokParser),
                                             NULL, 0));
                  break;
               case PARSER_VALUE_VMAX:
                  if (ZentraleGetVerbose(Data))
                     puts("lok vmax");
                  LokInfoSetVmax(ZentraleGetActualLok(Data),
                                 strtoul(Cs2pGetValue(LokParser),
                                         NULL, 0));
                  break;
               case PARSER_VALUE_VMIN:
                  if (ZentraleGetVerbose(Data))
                     puts("lok vmin");
                  LokInfoSetVmin(ZentraleGetActualLok(Data),
                                 strtoul(Cs2pGetValue(LokParser),
                                         NULL, 0));
                  break;
               case PARSER_VALUE_FKT:
                  if (ZentraleGetVerbose(Data))
                     puts("lok fkt");
                  FktIndex++;
                  break;
               case PARSER_VALUE_DAUER:
                  if (ZentraleGetVerbose(Data))
                     puts("lok function dauer");
                  LokInfoSetFktDauer(ZentraleGetActualLok(Data), FktIndex,
                                     strtoul(Cs2pGetValue(LokParser),
                                             NULL, 0));
                  break;
               case PARSER_VALUE_WERT:
                  if (ZentraleGetVerbose(Data))
                     puts("lok function wert");
                  LokInfoSetFktWert(ZentraleGetActualLok(Data), FktIndex,
                                    strtoul(Cs2pGetValue(LokParser),
                                            NULL, 0));
                  break;
            }
            break;
      }
   } while (LineInfo != PARSER_EOF);
   Cs2pDestroy(LokParser);
   LokInsert(ZentraleGetLoks(Data), ZentraleGetActualLok(Data));
   ZentraleSetActualIndex(Data, ZentraleGetActualIndex(Data) + 1);
   if (ZentraleGetActualIndex(Data) < ZentraleGetNumLoks(Data))
   {
      return(FALSE);
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         printf("save lokomotive.cs2\n");
      LokSaveLokomotiveCs2(ZentraleGetLoks(Data));
      return(TRUE);
   }
}

static void HandleCfgUnCompressedData(ZentraleStruct *Data, MrIpcCmdType *CmdFrame)
{  Cs2parser *Parser;

   Parser = Cs2pCreate();
   Cs2pInit(Parser, PARSER_TYPE_HEADER_CS2,
            Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
            Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
   Cs2pSetVerbose(Parser, FALSE);
   if (Cs2pParse(Parser) == PARSER_PARAGRAPH)
   {
      switch (Cs2pGetSubType(Parser))
      {
         case PARSER_PARAGRAPH_LOK:
         case PARSER_PARAGRAPH_NUMLOKS:
            HandleCfgLoknamen(Data, CmdFrame);
            CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
            CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKNAME);
            break;
         case PARSER_PARAGRAPH_LOKOMOTIVE:
            HandleCfgLokinfo(Data, CmdFrame);
            CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
            CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKINFO);
            break;
      }
      Cs2pDestroy(Parser);
   }
}

static void HandleCfgCompressedData(ZentraleStruct *Data, MrIpcCmdType *CmdFrame)
{  Cs2parser *Parser;

   if (ZentraleGetVerbose(Data))
      printf("FSM: inflate compressed cfg\n");
   ZFileInit(ZentraleGetPackedCs2File(Data),
             Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
             Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
   if (ZFileUnCompress(ZentraleGetPackedCs2File(Data)))
   {
      Parser = Cs2pCreate();
      Cs2pInit(Parser, PARSER_TYPE_HEADER_CS2,
               (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
               ZFileGetLength(ZentraleGetPackedCs2File(Data)));
      Cs2pSetVerbose(Parser, FALSE);
      if (Cs2pParse(Parser) == PARSER_PARAGRAPH)
      {
         switch (Cs2pGetSubType(Parser))
         {
            case PARSER_PARAGRAPH_LOKOMOTIVE:
               LokParseLokomotiveCs2(ZentraleGetLoks(Data),
                                     (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                             ZFileGetLength(ZentraleGetPackedCs2File(Data)));
               LokSaveLokomotiveCs2(ZentraleGetLoks(Data));
               break;
            case PARSER_PARAGRAPH_GLEISBILD:
               {  DIR *d;
                  struct dirent *dir;
                  char GleisbildPageDir[256], GleisbildPageFullName[256];

                  GleisbildClear(ZentraleGetGleisbild(Data));
                  strcpy(GleisbildPageDir, ZentraleGetLocPath(Data));
                  if (GleisbildPageDir[strlen(GleisbildPageDir) - 1] != '/')
                     strcat(GleisbildPageDir, "/");
                  strcat(GleisbildPageDir, MR_CS2_GLEISBILD_PAGE_SUBDIR);
                  d = opendir(GleisbildPageDir);
                  if (d)
                  {
                     while ((dir = readdir(d)) != NULL)
                     {
                        if ((strcmp(dir->d_name, ".") != 0) &&
                            (strcmp(dir->d_name, "..") != 0))
                        {
                           strcpy(GleisbildPageFullName, GleisbildPageDir);
                           if (GleisbildPageFullName[strlen(GleisbildPageFullName) - 1] != '/')
                              strcat(GleisbildPageFullName, "/");
                           strcat(GleisbildPageFullName, dir->d_name);
                           remove(GleisbildPageFullName);
                        }
                     }
                     closedir(d);
                  }
                  if (GleisbildGetNumPages(ZentraleGetGleisbild(Data)) > 0)
                  {  int i;

                     for (i=0; i<GleisbildGetNumPages(ZentraleGetGleisbild(Data)); i++)
                     {
                        GleisbildPageExit(ZentraleGetNrGleisPages(Data, i));
                        GleisbildPageDestroy(ZentraleGetNrGleisPages(Data, i));
                     }
                     free(ZentraleGetGleisPages(Data));
                  }
                  GleisbildParseGleisbildCs2(ZentraleGetGleisbild(Data),
                                             (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                             ZFileGetLength(ZentraleGetPackedCs2File(Data)));
                  GleisbildSaveGleisbildCs2(ZentraleGetGleisbild(Data));
                  if (GleisbildGetNumPages(ZentraleGetGleisbild(Data)) > 0)
                  {  int i;

                     ZentraleSetGleisPages(Data,
                                           (GleisbildPageStruct **)malloc(sizeof(GleisbildPageStruct *) *
                                                                          GleisbildGetNumPages(ZentraleGetGleisbild(Data))));
                     for (i=0; i<GleisbildGetNumPages(ZentraleGetGleisbild(Data)); i++)
                     {
                        ZentraleSetNrGleisPages(Data, i,
                                                (GleisbildPageStruct *)NULL);
                     }
                  }
               }
               break;
            case PARSER_PARAGRAPH_MAGNETARTIKEL:
               MagnetartikelParseMagnetartikelCs2(ZentraleGetMagnetartikel(Data),
                                                  (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                                  ZFileGetLength(ZentraleGetPackedCs2File(Data)));
               MagnetartikelSaveMagnetartikelCs2(ZentraleGetMagnetartikel(Data));
               break;
            case PARSER_PARAGRAPH_FAHRSTRASSEN:
               FahrstrasseParseFahrstrasseCs2(ZentraleGetFahrstrasse(Data),
                                              (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                              ZFileGetLength(ZentraleGetPackedCs2File(Data)));
               FahrstrasseSaveFahrstrasseCs2(ZentraleGetFahrstrasse(Data));
               break;
            case PARSER_PARAGRAPH_GLEISBILDSEITE:
               {
                  GleisbildPageStruct *NewPage;
                  char *GleisbildName;

                  NewPage = GleisbildPageCreate();
                  if (NewPage != (GleisbildPageStruct *)NULL)
                  {
                     GleisbildPageInit(NewPage, ZentraleGetLocPath(Data), "", 0);
                     GleisbildPageParseGleisbildPageCs2(NewPage,
                                                        (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                                        ZFileGetLength(ZentraleGetPackedCs2File(Data)));
                     GleisbildPageSetGleisbildPageFilePath(NewPage,
                                                           ZentraleGetLocPath(Data));
                     GleisbildName = malloc(strlen(GleisbildInfoGetName(GleisbildSearch(ZentraleGetGleisbild(Data),
                                                                        GleisbildPageStructGetPage(NewPage)))) +
                                            strlen(MR_CS2_FILE_EXTENSION) + 1);
                     if (GleisbildName != (char *)NULL)
                     {
                        strcpy(GleisbildName,
                               GleisbildInfoGetName(GleisbildSearch(ZentraleGetGleisbild(Data),
                                                    GleisbildPageStructGetPage(NewPage))));
                        strcat(GleisbildName, MR_CS2_FILE_EXTENSION);
                        GleisbildPageSetGleisbildName(NewPage,
                                                      GleisbildName);
                        GleisbildPageSaveGleisbildPageCs2(NewPage);
                        ZentraleSetNrGleisPages(Data,
                                                GleisbildPageStructGetPage(NewPage),
                                                NewPage);
                     }
                  }
               }
               break;
         }
      }
      Cs2pDestroy(Parser);
   }
   ZFileExit(ZentraleGetPackedCs2File(Data));
}

static BOOL DoCfgData(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   char Buf[8];

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetCfgData(CmdFrame, Buf);
   if (Cs2CfgDataNextBuf(ZentraleGetCs2CfgDaten(Data),
                         MrIpcGetCanHash(CmdFrame), Buf))
   {
      if (Cs2CfgDataGetIsCompressed(ZentraleGetCs2CfgDaten(Data),
                                    MrIpcGetCanHash(CmdFrame)))
      {
         if (ZentraleGetVerbose(Data))
            printf("FSM: handle compressed cfg\n");
         HandleCfgCompressedData(Data, CmdFrame);
      }
      else
      {
         if (ZentraleGetVerbose(Data))
            printf("FSM: handle uncompressed cfg\n");
         HandleCfgUnCompressedData(Data, CmdFrame);
      }
      Cs2CfgDataEnd(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame));
      if (ZentraleGetVerbose(Data))
         puts("FSM: cfg finished");
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      return(TRUE);
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         puts("FSM: cfg wait for next packet");
      return(FALSE);
   }
}

static int HandleCfgData(void *Priv, void *SignalData)
{
   DoCfgData(Priv, SignalData);
   return(STATE_NORMAL);
}

static int HandleLoknameCfgData(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd, *CmdFrame;
   char Buf[8];

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetCfgData(CmdFrame, Buf);
   if (Cs2CfgDataNextBuf(ZentraleGetCs2CfgDaten(Data),
                         MrIpcGetCanHash(CmdFrame), Buf))
   {
      if (HandleCfgLoknamen(Data, CmdFrame))
      {
         ZentraleSetActualIndex(Data, 0);
         if (ZentraleGetVerbose(Data))
            printf("request lokinfo %d >%s< from %d\n",
                   ZentraleGetActualIndex(Data),
                   ZentraleGetLokNamenNr(Data, ZentraleGetActualIndex(Data)),
                   ZentraleGetNumLoks(Data));
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MrIpcGetSenderSocket(CmdFrame));
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetReqestLocinfo(&Cmd,
                                  ZentraleGetLokNamenNr(Data,
                                                        ZentraleGetActualIndex(Data)));
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         if (ZentraleGetVerbose(Data))
            printf("FSM: new state %d\n",STATE_WAIT_LOKINFO_CFG_HDR);
         return(STATE_WAIT_LOKINFO_CFG_HDR);
      }
      else
      {
         if (ZentraleGetVerbose(Data))
            printf("request lokname %d\n", ZentraleGetActualIndex(Data));
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MrIpcGetSenderSocket(CmdFrame));
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetReqestLocname(&Cmd, ZentraleGetActualIndex(Data), 2);
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         if (ZentraleGetVerbose(Data))
            printf("FSM: new state %d\n",STATE_WAIT_LOKNAME_CFG_HDR);
         return(STATE_WAIT_LOKNAME_CFG_HDR);
      }
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: new state %d\n",STATE_WAIT_LOKNAME_CFG_DATA);
      return(STATE_WAIT_LOKNAME_CFG_DATA);
   }
}

static int HandleLokinfoCfgData(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd, *CmdFrame;
   char Buf[17];

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetCfgData(CmdFrame, Buf);
   if (Cs2CfgDataNextBuf(ZentraleGetCs2CfgDaten(Data),
                         MrIpcGetCanHash(CmdFrame), Buf))
   {
      if (HandleCfgLokinfo(Data, CmdFrame))
      {
         if (ZentraleGetVerbose(Data))
             printf("FSM: new state %d\n",STATE_NORMAL);
         return(STATE_NORMAL);
      }
      else
      {
         if (ZentraleGetVerbose(Data))
            printf("request lokinfo %d >%s< from %d\n",
                   ZentraleGetActualIndex(Data),
                   ZentraleGetLokNamenNr(Data,
                                         ZentraleGetActualIndex(Data)),
                   ZentraleGetNumLoks(Data));
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MrIpcGetSenderSocket(CmdFrame));
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetReqestLocinfo(&Cmd,
                                  ZentraleGetLokNamenNr(Data,
                                                        ZentraleGetActualIndex(Data)));
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         if (ZentraleGetVerbose(Data))
            printf("FSM: new state %d\n",STATE_WAIT_LOKNAME_CFG_HDR);
         return(STATE_WAIT_LOKINFO_CFG_HDR);
      }
   }
   else
   {
      if (ZentraleGetVerbose(Data))
      {
         printf("FSM: LOKINFO CFG data, aktuell %lu von %lu\n",
                Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
                Cs2CfgDataGetLength(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
         printf("FSM: new state %d\n",STATE_WAIT_LOKNAME_CFG_DATA);
      }
      return(STATE_WAIT_LOKINFO_CFG_DATA);
   }
}

static int HandleLokinfoWaitCfgData(void *Priv, void *SignalData)
{  int NextState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NextState = HandleLokinfoCfgData(Priv, SignalData);
   if (NextState == STATE_WAIT_LOKINFO_CFG_HDR)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      return(NextState);
   }
   else if (NextState == STATE_WAIT_LOKNAME_CFG_DATA)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      return(NextState);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_QUERY);
      if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_PERIODIC) != 0)
      {
         if (ZentraleGetVerbose(Data))
            puts("enable periodic poll of MS2");
         CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2);
      }
      else
      {
         if (ZentraleGetVerbose(Data))
            puts("disable periodic poll of MS2");
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2);
      }
      return(NextState);
   }
}

static int HandleLCfgDataProxy(void *Priv, void *SignalData,
                               int SignalContinue, int SignalStay,
                               char *CfgFile)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;

   Data = (ZentraleStruct *)Priv;
   if (DoCfgData(Priv, SignalData))
   {
      if (CfgFile != (char *)NULL)
      {
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetQuery(&Cmd, MR_CS2_CFG_DLC, CfgFile);
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      }
      if (ZentraleGetVerbose(Data))
         printf("FSM: rewrite new state %d\n",SignalContinue);
      return(SignalContinue);
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: rewrite new state %d\n",SignalStay);
      return(SignalStay);
   }
}

static int HandleGetLokCfgDataProxy(void *Priv, void *SignalData)
{  int NewState;
   ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleLCfgDataProxy(Priv, SignalData,
                                  STATE_NORMAL,
                                  STATE_GET_LOK_CS2_CFG_DATA, NULL);
   if (NewState == STATE_NORMAL)
   {
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   else
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   }
   return(NewState);
}

static int HandleLokCfgDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_KEYBD) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_MAG_CS2_CFG_HDR,
                                 STATE_WAIT_LOK_CS2_CFG_DATA, MR_CS2_CFG_MAGS));
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_MEM) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_FS_CS2_CFG_HDR,
                                 STATE_WAIT_LOK_CS2_CFG_DATA, MR_CS2_CFG_FS));
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_LAYOUT) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_GBS_CS2_CFG_HDR,
                                 STATE_WAIT_LOK_CS2_CFG_DATA, MR_CS2_CFG_GBS));
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_CONTR) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_LOK_CVR_CFG_HDR,
                                 STATE_WAIT_LOK_CS2_CFG_DATA,
                                 MR_CS2_CFG_LOK_STAT));
   else
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_LOK_CVR_CFG_HDR,
                                 STATE_WAIT_LOK_CS2_CFG_DATA,
                                 MR_CS2_CFG_LOK_STAT));
}

static int HandleMagCfgDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_MEM) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_FS_CS2_CFG_HDR,
                                 STATE_WAIT_MAG_CS2_CFG_DATA, MR_CS2_CFG_FS));
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_LAYOUT) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_GBS_CS2_CFG_HDR,
                                 STATE_WAIT_MAG_CS2_CFG_DATA, MR_CS2_CFG_GBS));
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_CONTR) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_LOK_CVR_CFG_HDR,
                                 STATE_WAIT_MAG_CS2_CFG_DATA,
                                 MR_CS2_CFG_LOK_STAT));
   else
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_LOK_CVR_CFG_HDR,
                                 STATE_WAIT_MAG_CS2_CFG_DATA,
                                 MR_CS2_CFG_LOK_STAT));
}

static int HandleFsCfgDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
   if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_LAYOUT) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_GBS_CS2_CFG_HDR,
                                 STATE_WAIT_FS_CS2_CFG_DATA, MR_CS2_CFG_GBS));
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_CONTR) != 0)
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_LOK_CVR_CFG_HDR,
                                 STATE_WAIT_FS_CS2_CFG_DATA,
                                 MR_CS2_CFG_LOK_STAT));
   else
      return(HandleLCfgDataProxy(Priv, SignalData, STATE_WAIT_LOK_CVR_CFG_HDR,
                                 STATE_WAIT_FS_CS2_CFG_DATA,
                                 MR_CS2_CFG_LOK_STAT));
}

static int HandleGbsCfgDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;
   char PageNummerStr[MR_CS2_NUM_CAN_BYTES + 1];

   Data = (ZentraleStruct *)Priv;
   if (DoCfgData(Priv, SignalData))
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: %d gleisbild pages\n",
                GleisbildGetNumPages(ZentraleGetGleisbild(Data)));
      ZentraleSetActualIndex(Data, 0);
      if (ZentraleGetActualIndex(Data) < GleisbildGetNumPages(ZentraleGetGleisbild(Data)))
      {
         if (ZentraleGetVerbose(Data))
            printf("FSM: fetch %d of %d\n", ZentraleGetActualIndex(Data),
                   GleisbildGetNumPages(ZentraleGetGleisbild(Data)));
         sprintf(PageNummerStr, "%s%d", MR_CS2_CFG_GBS_PAGE, ZentraleGetActualIndex(Data));
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetQuery(&Cmd, MR_CS2_CFG_DLC, PageNummerStr);
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         if (ZentraleGetVerbose(Data))
            printf("FSM: rewrite new state %d\n",STATE_WAIT_GPG_CS2_CFG_HDR);
         return(STATE_WAIT_GPG_CS2_CFG_HDR);
      }
      else
      {
         if (ZentraleGetVerbose(Data))
            printf("FSM: rewrite new state %d\n",STATE_NORMAL);
         return(STATE_NORMAL);
      }
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: rewrite new state %d\n",STATE_WAIT_GBS_CS2_CFG_DATA);
      return(STATE_WAIT_GBS_CS2_CFG_DATA);
   }
}

static int HandleGpgCfgDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;
   char PageNummerStr[9];

   Data = (ZentraleStruct *)Priv;
   if (DoCfgData(Priv, SignalData))
   {
      ZentraleSetActualIndex(Data, ZentraleGetActualIndex(Data) + 1);
      if (ZentraleGetActualIndex(Data) < GleisbildGetNumPages(ZentraleGetGleisbild(Data)))
      {
         if (ZentraleGetVerbose(Data))
            printf("FSM: fetch %d of %d\n", ZentraleGetActualIndex(Data),
                   GleisbildGetNumPages(ZentraleGetGleisbild(Data)));
         sprintf(PageNummerStr, "%s%d", MR_CS2_CFG_GBS_PAGE, ZentraleGetActualIndex(Data));
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetQuery(&Cmd, MR_CS2_CFG_DLC, PageNummerStr);
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         if (ZentraleGetVerbose(Data))
            printf("FSM: rewrite new state %d\n",STATE_WAIT_GPG_CS2_CFG_HDR);
         return(STATE_WAIT_GPG_CS2_CFG_HDR);
      }
      else
      {
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetQuery(&Cmd, MR_CS2_CFG_DLC, MR_CS2_CFG_LOK_STAT);
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         if (ZentraleGetVerbose(Data))
            printf("FSM: rewrite new state %d\n",STATE_WAIT_LOK_CVR_CFG_HDR);
         return(STATE_WAIT_LOK_CVR_CFG_HDR);
      }
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: rewrite new state %d\n",STATE_WAIT_GPG_CS2_CFG_DATA);
      return(STATE_WAIT_GPG_CS2_CFG_DATA);
   }
}

static int HandleCvrData(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   char Buf[8];
   Cs2parser *Parser;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetCfgData(CmdFrame, Buf);
   if (Cs2CfgDataNextBuf(ZentraleGetCs2CfgDaten(Data),
                         MrIpcGetCanHash(CmdFrame), Buf))
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: inflate compressed cfg\n");
      ZFileInit(ZentraleGetPackedCs2File(Data),
                Cs2CfgDataGetBuf(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)),
                Cs2CfgDataGetHaveRead(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame)));
      if (ZFileUnCompress(ZentraleGetPackedCs2File(Data)))
      {
         Parser = Cs2pCreate();
         Cs2pInit(Parser, PARSER_TYPE_HEADER_CS2,
                  (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                  ZFileGetLength(ZentraleGetPackedCs2File(Data)));
         Cs2pSetVerbose(Parser, FALSE);
         if (Cs2pParse(Parser) == PARSER_PARAGRAPH)
         {
            switch (Cs2pGetSubType(Parser))
            {
               case PARSER_PARAGRAPH_LOKSTATUS:
                  LokStatusParseLokomotiveSr2(ZentraleGetLoks(Data),
                                              (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                              ZFileGetLength(ZentraleGetPackedCs2File(Data)));
                  LokStatusSaveLokomotiveSr2(ZentraleGetLoks(Data));
                  break;
               case PARSER_PARAGRAPH_GLEISBILD:
                  GbsStatParseGbsStatSr2(ZentraleGetGleisbild(Data),
                                         (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                         ZFileGetLength(ZentraleGetPackedCs2File(Data)));
                  GbsStatSaveGbsStatSr2(ZentraleGetGleisbild(Data));
                  break;
               case PARSER_PARAGRAPH_MAGNETARTIKEL:
                  MagStatusParseMagStatusSr2(ZentraleGetMagnetartikel(Data),
                                             (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                             ZFileGetLength(ZentraleGetPackedCs2File(Data)));
                  MagStatusSaveMagStatusSr2(ZentraleGetMagnetartikel(Data));
                  break;
               case PARSER_PARAGRAPH_FAHRSTRASSEN:
                  FsStatParseFsStatSr2(ZentraleGetFahrstrasse(Data),
                                       (char *)ZFileGetBuffer(ZentraleGetPackedCs2File(Data)),
                                       ZFileGetLength(ZentraleGetPackedCs2File(Data)));
                  FsStatSaveFsStatSr2(ZentraleGetFahrstrasse(Data));
                  break;
            }
         }
         Cs2pDestroy(Parser);
      }
      ZFileExit(ZentraleGetPackedCs2File(Data));
      Cs2CfgDataEnd(ZentraleGetCs2CfgDaten(Data), MrIpcGetCanHash(CmdFrame));
      if (ZentraleGetVerbose(Data))
         printf("FSM: new state %d\n",STATE_NORMAL);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      return(STATE_NORMAL);
   }
   else
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: new state %d\n",STATE_WAIT_CFG_DATA);
      return(STATE_WAIT_CFG_DATA);
   }
}

static int HandleSCfgDataProxy(void *Priv, void *SignalData,
                               int SignalContinue, int SignalStay,
                               char *CfgFile)
{  ZentraleStruct *Data;
   MrIpcCmdType Cmd;
   int Ret;

   Data = (ZentraleStruct *)Priv;
   Ret = HandleCvrData(Priv, SignalData);
   if (Ret == STATE_WAIT_CFG_DATA)
   {
      if (ZentraleGetVerbose(Data))
         printf("FSM: rewrite new state %d\n",SignalStay);
      return(SignalStay);
   }
   else
   {
      if (CfgFile != (char *)NULL)
      {
         MrIpcInit(&Cmd);
         MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
         MrIpcSetCanResponse(&Cmd, 0);
         MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
         MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_CONFIG_QUERY);
         MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
         MrIpcCmdSetQuery(&Cmd, MR_CS2_CFG_DLC, CfgFile);
         MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      }
      if (ZentraleGetVerbose(Data))
         printf("FSM: rewrite new state %d\n",SignalContinue);
      return(SignalContinue);
   }
}

static int HandleLokCvrDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   int NewState;

   Data = (ZentraleStruct *)Priv;
   if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_KEYBD) != 0)
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_WAIT_MAG_CVR_CFG_HDR,
                                     STATE_WAIT_LOK_CVR_CFG_DATA, MR_CS2_CFG_MAG_STAT);
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_LAYOUT) != 0)
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_WAIT_GBS_CVR_CFG_HDR,
                                     STATE_WAIT_LOK_CVR_CFG_DATA, MR_CS2_CFG_GBS_STAT);
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_MEM) != 0)
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_WAIT_FS_CVR_CFG_HDR,
                                     STATE_WAIT_LOK_CVR_CFG_DATA, MR_CS2_CFG_FS_STAT);
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_CONTR) != 0)
   {
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                     STATE_WAIT_LOK_CVR_CFG_DATA, NULL);
      if (NewState == STATE_NORMAL)
      {
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      }
   }
   else
   {
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                     STATE_WAIT_LOK_CVR_CFG_DATA, NULL);
      if (NewState == STATE_NORMAL)
      {
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      }
   }
   return(NewState);
}

static int HandleMagCvrDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   int NewState;

   Data = (ZentraleStruct *)Priv;
   if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_LAYOUT) != 0)
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_WAIT_GBS_CVR_CFG_HDR,
                                     STATE_WAIT_MAG_CVR_CFG_DATA, MR_CS2_CFG_GBS_STAT);
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_MEM) != 0)
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_WAIT_FS_CVR_CFG_HDR,
                                     STATE_WAIT_MAG_CVR_CFG_DATA, MR_CS2_CFG_FS_STAT);
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_CONTR) != 0)
   {
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                     STATE_WAIT_MAG_CVR_CFG_DATA, NULL);
      if (NewState == STATE_NORMAL)
      {
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      }
   }
   else
   {
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                     STATE_WAIT_MAG_CVR_CFG_DATA, NULL);
      if (NewState == STATE_NORMAL)
      {
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      }
   }
   return(NewState);
}

static int HandleGbsCvrDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   int NewState;

   Data = (ZentraleStruct *)Priv;
   if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_MEM) != 0)
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_WAIT_FS_CVR_CFG_HDR,
                                     STATE_WAIT_GBS_CVR_CFG_DATA, MR_CS2_CFG_FS_STAT);
   else if ((ZentraleGetSyncMask(Data) & MRSYSTEM_CFG_SYNC_CONTR) != 0)
   {
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                     STATE_WAIT_GBS_CVR_CFG_DATA, NULL);
      if (NewState == STATE_NORMAL)
      {
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      }
   }
   else
   {
      NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                     STATE_WAIT_GBS_CVR_CFG_DATA, NULL);
      if (NewState == STATE_NORMAL)
      {
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      }
   }
   return(NewState);
}

static int HandleFsCvrDataProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   int NewState;

   Data = (ZentraleStruct *)Priv;
   NewState = HandleSCfgDataProxy(Priv, SignalData, STATE_NORMAL,
                                  STATE_WAIT_FS_CVR_CFG_DATA, NULL);
   if (NewState == STATE_NORMAL)
   {
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
   }
   return(NewState);
}

static void DoPingMember(ZentraleStruct *Data, unsigned int DeviceId)
{  MrIpcCmdType Cmd;

   MrIpcInit(&Cmd);
   MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
   MrIpcSetCanResponse(&Cmd, 1);
   MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
   MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_PING);
   MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
   MrIpcCmdSetMember(&Cmd, ZentraleGetUid(Data), 0x100, DeviceId);
   MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
}

static int HandlePing(void *Priv, void *SignalData)
{  ZentraleStruct *Data;

   Data = (ZentraleStruct *)Priv;
   if (ZentraleGetVerbose(Data))
      puts("FSM: answer ping");
   DoPingMember(Data, MR_CS2_DEVID_CS2);
   DoPingMember(Data, MR_CS2_DEVID_CS2GUI);
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static void PingAnswerToS88(ZentraleStruct *Data, CanMemberInfo *CanMember)
{  MrIpcCmdType Cmd;
   unsigned int i;

   if (ZentraleGetShouldWakeUpS88(Data) &&
       ((CanMemberInfoGetUid(CanMember) & 0xffff0000) == S88_UID_PREFIX))
   {
      for (i = 0; i < 3; i++)
      {
         if (ZentraleGetS88BusIdxLength(Data, i) != 0)
         {
            MrIpcInit(&Cmd);
            MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
            MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
            MrIpcSetCanResponse(&Cmd, 0);
            MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
            MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_SYSTEM);
            MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
            MrIpcCmdSetSystemStatusVal(&Cmd,
                                       CanMemberInfoGetUid(CanMember),
                                       i + 2,
                                       ZentraleGetS88BusIdxLength(Data, i));
            MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         }
      }
      for (i = 0; i < 3; i++)
      {
         if (ZentraleGetS88BusIdxTCycle(Data, i) != 0)
         {
            MrIpcInit(&Cmd);
            MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
            MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
            MrIpcSetCanResponse(&Cmd, 0);
            MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
            MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_SYSTEM);
            MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
            MrIpcCmdSetSystemStatusVal(&Cmd,
                                       CanMemberInfoGetUid(CanMember),
                                       i + 5,
                                       ZentraleGetS88BusIdxTCycle(Data, i));
            MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
         }
      }
   }
}

static int HandleMemberMs2Master(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Uid;
   unsigned int Version, Type;
   CanMemberInfo NewCanMember, *OldCanMember;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetMember(CmdFrame, &Uid, &Version, &Type);
   if (ZentraleGetVerbose(Data))
      printf("FSM: CAN member %lu, version %d, type 0x%x\n", Uid, Version, Type);
   OldCanMember = CanMemberSearch(ZentraleGetCanMember(Data), Uid);
   if (OldCanMember == (CanMemberInfo *)NULL)
   {
      CanMemberInfoSetUid(&NewCanMember, Uid);
      CanMemberInfoSetVersion(&NewCanMember, Version);
      CanMemberInfoSetType(&NewCanMember, Type);
      CanMemberInsert(ZentraleGetCanMember(Data), &NewCanMember);
      if (Type == MR_CS2_DEVID_CS2)
      {
         QueryMembers(Data);
         DoPingMember(Data, MR_CS2_DEVID_CS2);
         DoPingMember(Data, MR_CS2_DEVID_CS2GUI);
      }
      PingAnswerToS88(Data, &NewCanMember);
   }
   else
   {
      if (CanMemberInfoGetIsInvalid(OldCanMember))
      {
         CanMemberInfoSetIsInvalid(OldCanMember, FALSE);
      }
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleMemberWaitCs2Proxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Uid;
   unsigned int Version, Type;
   CanMemberInfo NewCanMember, *OldCanMember;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetMember(CmdFrame, &Uid, &Version, &Type);
   if (ZentraleGetVerbose(Data))
      printf("FSM: CAN member %lu, version %d, type 0x%x\n", Uid, Version, Type);
   OldCanMember = CanMemberSearch(ZentraleGetCanMember(Data), Uid);
   if (OldCanMember == (CanMemberInfo *)NULL)
   {
      CanMemberInfoSetUid(&NewCanMember, Uid);
      CanMemberInfoSetVersion(&NewCanMember, Version);
      CanMemberInfoSetType(&NewCanMember, Type);
      CanMemberInsert(ZentraleGetCanMember(Data), &NewCanMember);
      if (Type == MR_CS2_DEVID_CS2)
      {
         QueryMembers(Data);
         if (ZentraleGetVerbose(Data))
            printf("FSM: new state %d\n",STATE_WAIT_FOR_MS2);
         CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_CS2);
         CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      }
      PingAnswerToS88(Data, &NewCanMember);
   }
   else
   {
      CanMemberInfoSetIsInvalid(OldCanMember, FALSE);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleMemberProxy(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Uid;
   unsigned int Version, Type;
   CanMemberInfo NewCanMember, *OldCanMember;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   MrIpcCmdGetMember(CmdFrame, &Uid, &Version, &Type);
   if (ZentraleGetVerbose(Data))
      printf("FSM: CAN member %lu, version %d, type 0x%x\n", Uid, Version, Type);
   OldCanMember = CanMemberSearch(ZentraleGetCanMember(Data), Uid);
   if (OldCanMember == (CanMemberInfo *)NULL)
   {
      CanMemberInfoSetUid(&NewCanMember, Uid);
      CanMemberInfoSetVersion(&NewCanMember, Version);
      CanMemberInfoSetType(&NewCanMember, Type);
      CanMemberInsert(ZentraleGetCanMember(Data), &NewCanMember);
      PingAnswerToS88(Data, &NewCanMember);
   }
   else
   {
      CanMemberInfoSetIsInvalid(OldCanMember, FALSE);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleCanBootldr(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame, Cmd;
   unsigned long Uid;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   if (ZentraleGetVerbose(Data))
      printf("FSM: CAN Bootloader Gebunden %d\n", MrIpcGetCommand(CmdFrame));
   Uid = GetLongFromByteArray(MrIpcGetRawData(CmdFrame));
   if (ZentraleGetShouldWakeUpS88(Data) &&
       (MrIpcGetRawDlc(CmdFrame) == 8) &&
       ((Uid & 0xffff0000) == S88_UID_PREFIX) &&
       (CanMemberSearch(ZentraleGetCanMember(Data), Uid) != (CanMemberInfo *)NULL))
   {
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_BOOTLDR_CAN);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetCanBootldr(&Cmd, S88Bootldr[0].Length, S88Bootldr[0].Data);
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_BOOTLDR_CAN);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetCanBootldr(&Cmd, S88Bootldr[1].Length, S88Bootldr[1].Data);
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      MrIpcInit(&Cmd);
      MrIpcSetSenderSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetReceiverSocket(&Cmd, MR_IPC_SOCKET_ALL);
      MrIpcSetCanResponse(&Cmd, 0);
      MrIpcCalcHash(&Cmd, ZentraleGetUid(Data));
      MrIpcSetCanCommand(&Cmd, MR_CS2_CMD_BOOTLDR_CAN);
      MrIpcSetCanPrio(&Cmd, MR_CS2_PRIO_2);
      MrIpcCmdSetCanBootldr(&Cmd, S88Bootldr[2].Length, S88Bootldr[2].Data);
      MrIpcSend(ZentraleGetClientSock(Data), &Cmd);
      QueryMembers(Data);
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleStatusRequest(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;
   unsigned long Addr;
   unsigned int Index;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   if (ZentraleGetVerbose(Data))
      printf("FSM: Status %d\n", MrIpcGetCommand(CmdFrame));
   MrIpcCmdGetStatusRequest(CmdFrame, &Addr, &Index);
   if (Addr == ZentraleGetUid(Data))
   {
      if (ZentraleGetVerbose(Data))
         puts("we should provide status values/konfiguration");
   }
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static int HandleOther(void *Priv, void *SignalData)
{  ZentraleStruct *Data;
   MrIpcCmdType *CmdFrame;

   Data = (ZentraleStruct *)Priv;
   CmdFrame = (MrIpcCmdType *)SignalData;
   if (ZentraleGetVerbose(Data))
      printf("FSM: unhandled %d\n", MrIpcGetCommand(CmdFrame));
   if (ZentraleGetVerbose(Data))
      printf("FSM: new state %d\n",STATE_NO_CHANGE);
   return(STATE_NO_CHANGE);
}

static StateFktType StateProxyWaitForMs2[NUM_SIGNALS] = {
   /* STATE_WAIT_FOR_MS2 */
   HandleTimer,         /* timer */
   HandleOther,         /* MrIpcCmdNull */
   HandleOther,         /* MrIpcCmdRun */
   HandleOther,         /* MrIpcTrackProto */
   HandleOther,         /* MrIpcCmdLocomotiveSpeed */
   HandleOther,         /* MrIpcCmdLocomotiveDirection */
   HandleOther,         /* MrIpcCmdLocomotiveFunction */
   HandleOther,         /* MrIpcCmdAccSwitch */
   HandlePing,          /* MrIpcCmdRequestMember */
   HandleOther,         /* MrIpcCmdMember */
   HandleOther,         /* MrIpcCmdRequestLocName */
   HandleOther,         /* MrIpcCmdRequestLocInfo */
   HandleOther,         /* MrIpcCmdRequestFile */
   HandleOther,         /* MrIpcCmdCfgHeader */
   HandleOther,         /* MrIpcCmdCfgZHeader */
   HandleOther,         /* MrIpcCmdCfgData */
   HandleOther,         /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,    /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest, /* MrIpcCmdStatusRequest */
   HandleOther,         /* MrIpcCmdStatusSize */
   HandleOther          /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterWaitForMs2[NUM_SIGNALS] = {
   /* STATE_WAIT_FOR_MS2 */
   HandleTimer,         /* timer */
   HandleOther,         /* MrIpcCmdNull */
   HandleOther,         /* MrIpcCmdRun */
   HandleOther,         /* MrIpcTrackProto */
   HandleOther,         /* MrIpcCmdLocomotiveSpeed */
   HandleOther,         /* MrIpcCmdLocomotiveDirection */
   HandleOther,         /* MrIpcCmdLocomotiveFunction */
   HandleOther,         /* MrIpcCmdAccSwitch */
   HandleOther,         /* MrIpcCmdRequestMember */
   HandleMemberWaitMs2, /* MrIpcCmdMember */
   HandleOther,         /* MrIpcCmdRequestLocName */
   HandleOther,         /* MrIpcCmdRequestLocInfo */
   HandleOther,         /* MrIpcCmdRequestFile */
   HandleOther,         /* MrIpcCmdCfgHeader */
   HandleOther,         /* MrIpcCmdCfgZHeader */
   HandleOther,         /* MrIpcCmdCfgData */
   HandleOther,         /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,    /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest, /* MrIpcCmdStatusRequest */
   HandleOther,         /* MrIpcCmdStatusSize */
   HandleOther          /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterWaitLoknameCfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_LOKNAME_CFG_HDR */
   HandleTimer,                /* timer */
   HandleOther,                /* MrIpcCmdNull */
   HandleOther,                /* MrIpcCmdRun */
   HandleOther,                /* MrIpcTrackProto */
   HandleOther,                /* MrIpcCmdLocomotiveSpeed */
   HandleOther,                /* MrIpcCmdLocomotiveDirection */
   HandleOther,                /* MrIpcCmdLocomotiveFunction */
   HandleOther,                /* MrIpcCmdAccSwitch */
   HandleOther,                /* MrIpcCmdRequestMember */
   HandleMemberMs2Master,      /* MrIpcCmdMember */
   HandleOther,                /* MrIpcCmdRequestLocName */
   HandleOther,                /* MrIpcCmdRequestLocInfo */
   HandleOther,                /* MrIpcCmdRequestFile */
   HandleLoknameWaitCfgHeader, /* MrIpcCmdCfgHeader */
   HandleOther,                /* MrIpcCmdCfgZHeader */
   HandleOther,                /* MrIpcCmdCfgData */
   HandleOther,                /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,           /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,        /* MrIpcCmdStatusRequest */
   HandleOther,                /* MrIpcCmdStatusSize */
   HandleOther                 /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterWaitLoknameCfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_LOKNAME_CFG_DATA */
   HandleTimer,           /* timer */
   HandleOther,           /* MrIpcCmdNull */
   HandleOther,           /* MrIpcCmdRun */
   HandleOther,           /* MrIpcTrackProto */
   HandleOther,           /* MrIpcCmdLocomotiveSpeed */
   HandleOther,           /* MrIpcCmdLocomotiveDirection */
   HandleOther,           /* MrIpcCmdLocomotiveFunction */
   HandleOther,           /* MrIpcCmdAccSwitch */
   HandleOther,           /* MrIpcCmdRequestMember */
   HandleMemberMs2Master, /* MrIpcCmdMember */
   HandleOther,           /* MrIpcCmdRequestLocName */
   HandleOther,           /* MrIpcCmdRequestLocInfo */
   HandleOther,           /* MrIpcCmdRequestFile */
   HandleOther,           /* MrIpcCmdCfgHeader */
   HandleOther,           /* MrIpcCmdCfgZHeader */
   HandleLoknameCfgData,  /* MrIpcCmdCfgData */
   HandleOther,           /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,      /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,   /* MrIpcCmdStatusRequest */
   HandleOther,           /* MrIpcCmdStatusSize */
   HandleOther            /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterWaitLokinfoCfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_LOKINFO_CFG_HDR */
   HandleTimer,                /* timer */
   HandleOther,                /* MrIpcCmdNull */
   HandleOther,                /* MrIpcCmdRun */
   HandleOther,                /* MrIpcTrackProto */
   HandleOther,                /* MrIpcCmdLocomotiveSpeed */
   HandleOther,                /* MrIpcCmdLocomotiveDirection */
   HandleOther,                /* MrIpcCmdLocomotiveFunction */
   HandleOther,                /* MrIpcCmdAccSwitch */
   HandleOther,                /* MrIpcCmdRequestMember */
   HandleMemberMs2Master,      /* MrIpcCmdMember */
   HandleOther,                /* MrIpcCmdRequestLocName */
   HandleOther,                /* MrIpcCmdRequestLocInfo */
   HandleOther,                /* MrIpcCmdRequestFile */
   HandleLokinfoWaitCfgHeader, /* MrIpcCmdCfgHeader */
   HandleOther,                /* MrIpcCmdCfgZHeader */
   HandleOther,                /* MrIpcCmdCfgData */
   HandleOther,                /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,           /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,        /* MrIpcCmdStatusRequest */
   HandleOther,                /* MrIpcCmdStatusSize */
   HandleOther                 /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterWaitLokinfoCfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_LOKINFO_CFG_DATA */
   HandleTimer,              /* timer */
   HandleOther,              /* MrIpcCmdNull */
   HandleOther,              /* MrIpcCmdRun */
   HandleOther,              /* MrIpcTrackProto */
   HandleOther,              /* MrIpcCmdLocomotiveSpeed */
   HandleOther,              /* MrIpcCmdLocomotiveDirection */
   HandleOther,              /* MrIpcCmdLocomotiveFunction */
   HandleOther,              /* MrIpcCmdAccSwitch */
   HandleOther,              /* MrIpcCmdRequestMember */
   HandleMemberMs2Master,    /* MrIpcCmdMember */
   HandleOther,              /* MrIpcCmdRequestLocName */
   HandleOther,              /* MrIpcCmdRequestLocInfo */
   HandleOther,              /* MrIpcCmdRequestFile */
   HandleOther,              /* MrIpcCmdCfgHeader */
   HandleOther,              /* MrIpcCmdCfgZHeader */
   HandleLokinfoWaitCfgData, /* MrIpcCmdCfgData */
   HandleOther,              /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,         /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,      /* MrIpcCmdStatusRequest */
   HandleOther,              /* MrIpcCmdStatusSize */
   HandleOther               /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterWaitCfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandleOther,          /* MrIpcCmdRequestMember */
   HandleMemberMs2Master,/* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleCfgData,        /* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitCs[NUM_SIGNALS] = {
   /* STATE_WAIT_CS2 */
   HandleTimer,               /* timer */
   HandleOther,               /* MrIpcCmdNull */
   HandleOther,               /* MrIpcCmdRun */
   HandleOther,               /* MrIpcTrackProto */
   HandleLokSpeed,            /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,        /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,         /* MrIpcCmdLocomotiveFunction */
   HandleOther,               /* MrIpcCmdAccSwitch */
   HandleOther,               /* MrIpcCmdRequestMember */
   HandleMemberWaitCs2Proxy,  /* MrIpcCmdMember */
   HandleOther,               /* MrIpcCmdRequestLocName */
   HandleOther,               /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,         /* MrIpcCmdRequestFile */
   HandleGetLokCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleGetLokCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,               /* MrIpcCmdCfgData */
   HandleOther,               /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,          /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,       /* MrIpcCmdStatusRequest */
   HandleOther,               /* MrIpcCmdStatusSize */
   HandleOther                /* MrIpcCmdStatusData */
};
static StateFktType StateProxyNormal[NUM_SIGNALS] = {
   /* STATE_NORMAL */
   HandleTimer,               /* timer */
   HandleOther,               /* MrIpcCmdNull */
   HandleOther,               /* MrIpcCmdRun */
   HandleOther,               /* MrIpcTrackProto */
   HandleLokSpeed,            /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,        /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,         /* MrIpcCmdLocomotiveFunction */
   HandleOther,               /* MrIpcCmdAccSwitch */
   HandlePing,                /* MrIpcCmdRequestMember */
   HandleMemberProxy,         /* MrIpcCmdMember */
   HandleOther,               /* MrIpcCmdRequestLocName */
   HandleOther,               /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,         /* MrIpcCmdRequestFile */
   HandleGetLokCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleGetLokCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,               /* MrIpcCmdCfgData */
   HandleOther,               /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,          /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,       /* MrIpcCmdStatusRequest */
   HandleOther,               /* MrIpcCmdStatusSize */
   HandleOther                /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitLokCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_LOK_CS2_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleLokCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleLokCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyGetGetLokCs2CfgData[NUM_SIGNALS] = {
   /* STATE_GET_LOK_CS2_CFG_DATA */
   HandleTimer,             /* timer */
   HandleOther,             /* MrIpcCmdNull */
   HandleOther,             /* MrIpcCmdRun */
   HandleOther,             /* MrIpcTrackProto */
   HandleLokSpeed,          /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,      /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,       /* MrIpcCmdLocomotiveFunction */
   HandleOther,             /* MrIpcCmdAccSwitch */
   HandlePing,              /* MrIpcCmdRequestMember */
   HandleMemberProxy,       /* MrIpcCmdMember */
   HandleOther,             /* MrIpcCmdRequestLocName */
   HandleOther,             /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,       /* MrIpcCmdRequestFile */
   HandleOther,             /* MrIpcCmdCfgHeader */
   HandleOther,             /* MrIpcCmdCfgZHeader */
   HandleGetLokCfgDataProxy,/* MrIpcCmdCfgData */
   HandleOther,             /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,        /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,     /* MrIpcCmdStatusRequest */
   HandleOther,             /* MrIpcCmdStatusSize */
   HandleOther              /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitLokCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_LOK_CS2_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleLokCfgDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitMagCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_MAG_CS2_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleMagCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleMagCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitMagCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_MAG_CS2_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleMagCfgDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitFsCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_FS_CS2_CFG_HDR */
   HandleTimer,           /* timer */
   HandleOther,           /* MrIpcCmdNull */
   HandleOther,           /* MrIpcCmdRun */
   HandleOther,           /* MrIpcTrackProto */
   HandleLokSpeed,        /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,    /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,     /* MrIpcCmdLocomotiveFunction */
   HandleOther,           /* MrIpcCmdAccSwitch */
   HandlePing,            /* MrIpcCmdRequestMember */
   HandleMemberProxy,     /* MrIpcCmdMember */
   HandleOther,           /* MrIpcCmdRequestLocName */
   HandleOther,           /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,     /* MrIpcCmdRequestFile */
   HandleFsCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleFsCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,           /* MrIpcCmdCfgData */
   HandleOther,           /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,      /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,   /* MrIpcCmdStatusRequest */
   HandleOther,           /* MrIpcCmdStatusSize */
   HandleOther            /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitFsCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_FS_CS2_CFG_DATA */
   HandleTimer,         /* timer */
   HandleOther,         /* MrIpcCmdNull */
   HandleOther,         /* MrIpcCmdRun */
   HandleOther,         /* MrIpcTrackProto */
   HandleLokSpeed,      /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,  /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,   /* MrIpcCmdLocomotiveFunction */
   HandleOther,         /* MrIpcCmdAccSwitch */
   HandlePing,          /* MrIpcCmdRequestMember */
   HandleMemberProxy,   /* MrIpcCmdMember */
   HandleOther,         /* MrIpcCmdRequestLocName */
   HandleOther,         /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,   /* MrIpcCmdRequestFile */
   HandleOther,         /* MrIpcCmdCfgHeader */
   HandleOther,         /* MrIpcCmdCfgZHeader */
   HandleFsCfgDataProxy,/* MrIpcCmdCfgData */
   HandleOther,         /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,    /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest, /* MrIpcCmdStatusRequest */
   HandleOther,         /* MrIpcCmdStatusSize */
   HandleOther          /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitGbsCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CS2_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleGbsCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleGbsCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitGbsCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CS2_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleGbsCfgDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitGpgCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CS2_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleGpgCfgHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleGpgCfgHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitGpgCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CS2_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleGpgCfgDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitLokCvrCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_LOK_CVR_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleLokCvrHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleLokCvrHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitLokCvrCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_LOK_CVR_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleLokCvrDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitMagCvrCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_MAG_CVR_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleMagCvrHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleMagCvrHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitMagCvrCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_MAG_CVR_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleMagCvrDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};

static StateFktType StateProxyWaitGbsCvrCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CVR_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleGbsCvrHeaderProxy,/* MrIpcCmdCfgHeader */
   HandleGbsCvrHeaderProxy,/* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitGbsCvrCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CVR_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleGbsCvrDataProxy,/* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitFsCvrCs2CfgHdr[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CVR_CFG_HDR */
   HandleTimer,            /* timer */
   HandleOther,            /* MrIpcCmdNull */
   HandleOther,            /* MrIpcCmdRun */
   HandleOther,            /* MrIpcTrackProto */
   HandleLokSpeed,         /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,     /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,      /* MrIpcCmdLocomotiveFunction */
   HandleOther,            /* MrIpcCmdAccSwitch */
   HandlePing,             /* MrIpcCmdRequestMember */
   HandleMemberProxy,      /* MrIpcCmdMember */
   HandleOther,            /* MrIpcCmdRequestLocName */
   HandleOther,            /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,      /* MrIpcCmdRequestFile */
   HandleFsCvrHeaderProxy, /* MrIpcCmdCfgHeader */
   HandleFsCvrHeaderProxy, /* MrIpcCmdCfgZHeader */
   HandleOther,            /* MrIpcCmdCfgData */
   HandleOther,            /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,       /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,    /* MrIpcCmdStatusRequest */
   HandleOther,            /* MrIpcCmdStatusSize */
   HandleOther             /* MrIpcCmdStatusData */
};
static StateFktType StateProxyWaitFsCvrCs2CfgData[NUM_SIGNALS] = {
   /* STATE_WAIT_GBS_CVR_CFG_DATA */
   HandleTimer,          /* timer */
   HandleOther,          /* MrIpcCmdNull */
   HandleOther,          /* MrIpcCmdRun */
   HandleOther,          /* MrIpcTrackProto */
   HandleLokSpeed,       /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,   /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,    /* MrIpcCmdLocomotiveFunction */
   HandleOther,          /* MrIpcCmdAccSwitch */
   HandlePing,           /* MrIpcCmdRequestMember */
   HandleMemberProxy,    /* MrIpcCmdMember */
   HandleOther,          /* MrIpcCmdRequestLocName */
   HandleOther,          /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,    /* MrIpcCmdRequestFile */
   HandleOther,          /* MrIpcCmdCfgHeader */
   HandleOther,          /* MrIpcCmdCfgZHeader */
   HandleFsCvrDataProxy, /* MrIpcCmdCfgData */
   HandleOther,          /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,     /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,  /* MrIpcCmdStatusRequest */
   HandleOther,          /* MrIpcCmdStatusSize */
   HandleOther           /* MrIpcCmdStatusData */
};
static StateFktType StateMs2MasterNormal[NUM_SIGNALS] = {
   /* STATE_NORMAL */
   HandleTimer,                 /* timer */
   HandleOther,                 /* MrIpcCmdNull */
   HandleOther,                 /* MrIpcCmdRun */
   HandleOther,                 /* MrIpcTrackProto */
   HandleLokSpeed,              /* MrIpcCmdLocomotiveSpeed */
   HandleLokDirection,          /* MrIpcCmdLocomotiveDirection */
   HandleLokFunction,           /* MrIpcCmdLocomotiveFunction */
   HandleOther,                 /* MrIpcCmdAccSwitch */
   HandlePing,                  /* MrIpcCmdRequestMember */
   HandleMemberMs2Master,       /* MrIpcCmdMember */
   HandleOther,                 /* MrIpcCmdRequestLocName */
   HandleOther,                 /* MrIpcCmdRequestLocInfo */
   HandleFileRequest,           /* MrIpcCmdRequestFile */
   HandleCfgHeaderUncompressed, /* MrIpcCmdCfgHeader */
   HandleCfgHeaderCompressed,   /* MrIpcCmdCfgZHeader */
   HandleCfgData,               /* MrIpcCmdCfgData */
   HandleOther,                 /* MrIpcCmdSystemStatusVal */
   HandleCanBootldr,            /* MrIpcCmdCanBootldrGeb */
   HandleStatusRequest,         /* MrIpcCmdStatusRequest */
   HandleOther,                 /* MrIpcCmdStatusSize */
   HandleOther                  /* MrIpcCmdStatusData */
};
static SignalFunctionsType StateMachineFunctionsProxy[NUM_STATES] = {
   StateProxyWaitForMs2,          /* STATE_WAIT_FOR_MS2 */
   (SignalFunctionsType)NULL,     /* STATE_WAIT_LOKNAME_CFG_HDR */
   (SignalFunctionsType)NULL,     /* STATE_WAIT_LOKNAME_CFG_DATA */
   (SignalFunctionsType)NULL,     /* STATE_WAIT_LOKINFO_CFG_HDR */
   (SignalFunctionsType)NULL,     /* STATE_WAIT_LOKINFO_CFG_DATA */
   (SignalFunctionsType)NULL,     /* STATE_WAIT_CFG_DATA */
   StateProxyWaitCs,              /* STATE_WAIT_CS2 */
   StateProxyNormal,              /* STATE_NORMAL */
   StateProxyGetGetLokCs2CfgData, /* STATE_GET_LOK_CS2_CFG_DATA */
   StateProxyWaitLokCs2CfgHdr,    /* STATE_WAIT_LOK_CS2_CFG_HDR */
   StateProxyWaitLokCs2CfgData,   /* STATE_WAIT_LOK_CS2_CFG_DATA */
   StateProxyWaitMagCs2CfgHdr,    /* STATE_WAIT_MAG_CS2_CFG_HDR */
   StateProxyWaitMagCs2CfgData,   /* STATE_WAIT_MAG_CS2_CFG_DATA */
   StateProxyWaitFsCs2CfgHdr,     /* STATE_WAIT_FS_CS2_CFG_HDR */
   StateProxyWaitFsCs2CfgData,    /* STATE_WAIT_FS_CS2_CFG_DATA */
   StateProxyWaitGbsCs2CfgHdr,    /* STATE_WAIT_GBS_CS2_CFG_HDR */
   StateProxyWaitGbsCs2CfgData,   /* STATE_WAIT_GBS_CS2_CFG_DATA */
   StateProxyWaitGpgCs2CfgHdr,    /* STATE_WAIT_GPG_CS2_CFG_HDR */
   StateProxyWaitGpgCs2CfgData,   /* STATE_WAIT_GPG_CS2_CFG_DATA */
   StateProxyWaitLokCvrCs2CfgHdr, /* STATE_WAIT_LOK_CVR_CFG_HDR */
   StateProxyWaitLokCvrCs2CfgData,/* STATE_WAIT_LOK_CVR_CFG_DATA */
   StateProxyWaitMagCvrCs2CfgHdr, /* STATE_WAIT_MAG_CVR_CFG_HDR */
   StateProxyWaitMagCvrCs2CfgData,/* STATE_WAIT_MAG_CVR_CFG_DATA */
   StateProxyWaitGbsCvrCs2CfgHdr, /* STATE_WAIT_GBS_CVR_CFG_HDR */
   StateProxyWaitGbsCvrCs2CfgData,/* STATE_WAIT_GBS_CVR_CFG_DATA */
   StateProxyWaitFsCvrCs2CfgHdr,  /* STATE_WAIT_FS_CVR_CFG_HDR */
   StateProxyWaitFsCvrCs2CfgData, /* STATE_WAIT_FS_CVR_CFG_DATA */
};
static SignalFunctionsType StateMachineFunctionsMs2Master[NUM_STATES] = {
   StateMs2MasterWaitForMs2,        /* STATE_WAIT_FOR_MS2 */
   StateMs2MasterWaitLoknameCfgHdr, /* STATE_WAIT_LOKNAME_CFG_HDR */
   StateMs2MasterWaitLoknameCfgData,/* STATE_WAIT_LOKNAME_CFG_DATA */
   StateMs2MasterWaitLokinfoCfgHdr, /* STATE_WAIT_LOKINFO_CFG_HDR */
   StateMs2MasterWaitLokinfoCfgData,/* STATE_WAIT_LOKINFO_CFG_DATA */
   StateMs2MasterWaitCfgData,       /* STATE_WAIT_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_CS2 */
   StateMs2MasterNormal,            /* STATE_NORMAL */
   (SignalFunctionsType)NULL,       /* STATE_GET_LOK_CS2_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_LOK_CS2_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_LOK_CS2_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_MAG_CS2_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_MAG_CS2_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_FS_CS2_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_FS_CS2_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_GBS_CS2_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_GBS_CS2_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_GPG_CS2_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_GPG_CS2_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_LOK_CVR_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_LOK_CVR_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_MAG_CVR_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_MAG_CVR_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_GBS_CVR_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_GBS_CVR_CFG_DATA */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_FS_CVR_CFG_HDR */
   (SignalFunctionsType)NULL,       /* STATE_WAIT_FS_CVR_CFG_DATA */
};
static SignalFunctionsType *StateMachines[] = {
   StateMachineFunctionsProxy,
   StateMachineFunctionsMs2Master,
};
static int StartStates[] = {
   STATE_WAIT_CS2,
   STATE_WAIT_FOR_MS2,
};

void ZentraleInitFsm(ZentraleStruct *Data, int MasterMode)
{
   if (MasterMode == 0)
   {
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING,
              PERIODIC_INTERVALL_PING, PeriodicPing,
              (void *)Data);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_PING);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_CS2,
              PERIODIC_INTERVALL_TIMEOUT, PeriodicTimeoutCs2,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_CS2);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL,
              PERIODIC_INTERVALL_TIMEOUT, PeriodicTimeoutWaitNormal,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_END_QUERY,
              PERIODIC_INTERVALL_QPERIOD, PeriodicEndQueryMembers,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_END_QUERY);
   }
   else if (MasterMode == 1)
   {
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_QUERY,
              PERIODIC_INTERVALL_QUERY, PeriodicQueryMembers,
              (void *)Data);
      CronEnable(ZentraleGetCronJobs(Data), PERIODIC_NAME_QUERY);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2,
              PERIODIC_INTERVALL_POLLMS2, PeriodicPollMs2,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_POLLMS2);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2,
              PERIODIC_INTERVALL_TIMEOUT, PeriodicTimeoutWaitMS2,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_MS2);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKNAME,
              PERIODIC_INTERVALL_LOKNAME, PeriodicQueryLokname,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKNAME);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKINFO,
              PERIODIC_INTERVALL_LOKINFO, PeriodicQueryLokinfo,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_LOKINFO);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL,
              PERIODIC_INTERVALL_TIMEOUT, PeriodicTimeoutWaitNormal,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_TO_NORMAL);
      CronAdd(ZentraleGetCronJobs(Data), PERIODIC_NAME_END_QUERY,
              PERIODIC_INTERVALL_QPERIOD, PeriodicEndQueryMembers,
              (void *)Data);
      CronDisable(ZentraleGetCronJobs(Data), PERIODIC_NAME_END_QUERY);
   }
   FsmInit(ZentraleGetStateMachine(Data), (void *)Data,
           StartStates[MasterMode], NUM_SIGNALS, NUM_STATES,
           StateMachines[MasterMode]);
}
