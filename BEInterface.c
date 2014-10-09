
#include "CWAC.h"
#include "ACAppsProtocol.h"
#include "CWVendorPayloads.h"
#include "BECommon.h"
#include "BELib.h"

#define LISTEN_PORT 8888
//#define LISTEN_PORT 5246
#define COMMAND_BUFFER_SIZE 5120
#define WTP_LIST_BUFFER_SIZE 1024

#define BE_TYPE_SIZE	sizeof(unsigned short)
#define BE_LENGTH_SIZE	sizeof(unsigned short)
int is_valid_wtp_index(int index);
int Readn(int sock, void *buf, size_t n);

/********************************************************************
 * Now the only parameter need by the application thread manager	*
 * is the index of socket.											*
 ********************************************************************/

typedef struct {
	int index;
} CWInterfaceThreadArg;

char BESetApValues(char* apMac, int socketIndex, CWVendorXMLValues* xmlValues)
{
	int numActiveWTPs =0,k = 0,i,j;
	//CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);	
	if(!CWErr(CWThreadMutexLock(&gActiveWTPsMutex))) {
		CWLog("Error locking the gActiveWTPsMutex mutex");
		return FALSE;
	}
	numActiveWTPs = gActiveWTPs;
	CWThreadMutexUnlock(&gActiveWTPsMutex);
	
	k = numActiveWTPs;
	if(!k)
	{
		CWLog("numActiveWTPs = 0,no connect ap !");
		return FALSE;
	}
	
	if(!CWErr(CWThreadMutexLock(&gWTPsMutex))) {
		CWLog("Error locking the gWTPsMutex mutex");
		return FALSE;
	}
	
	for(i=0; i<CW_MAX_WTP && k ; i++) 
	{
		if(gWTPs[i].isNotFree && gWTPs[i].currentState == CW_ENTER_RUN)  
		{
			k--;
			for (j = 0; j < MAC_ADDR_LEN; j++) 
			{
				if (apMac[j] == gWTPs[i].MAC[j]) 
				{
					if (j == (MAC_ADDR_LEN - 1))
					{	//CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
						CWThreadMutexUnlock(&gWTPsMutex);
						if(!CWXMLSetValues(i, socketIndex, xmlValues))
							return FALSE;
						break;
					}
				}
			}
		}
	}
	CWThreadMutexUnlock(&gWTPsMutex);
	//CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);	
	return TRUE;
}

char* AssembleBEheader(char* buf,int *len,int apId)
{
	BEHeader beHeader;
	char *rsp = NULL;
	int i,packetLen = 0;
	time_t timestamp;
	
	//CWLog("[F:%s, L:%d] :AssembleBEheader  *len = %d,apId = %d",__FILE__,__LINE__,*len, apId);
	
	beHeader.length =*len  + TIME_LEN + MAC_ADDR_LEN;
	packetLen = BE_TYPELEN_LEN + beHeader.length; 
	

	//sprintf(cmd,"date \"+%s\"");
	
	time(&timestamp);
	CWLog("[F:%s, L:%d] :beHeader.timestamp = %d",__FILE__,__LINE__,timestamp);
	
	beHeader.timestamp =Swap32(timestamp);
	//beHeader.timestamp =(timestamp);
//	CWLog("[F:%s, L:%d] :sizeof(int) = %d,beHeader.timestamp = %x",__FILE__,__LINE__,sizeof(unsigned int), beHeader.timestamp);	
//	CWLog("[F:%s, L:%d] :beHeader.type = %x",__FILE__,__LINE__,beHeader.type);	
	beHeader.type = htons(BE_CAPWAP_HEADER);
//	CWLog("[F:%s, L:%d] :beHeader.type = %x",__FILE__,__LINE__,beHeader.type);	
	CWLog("[F:%s, L:%d] :beHeader.length = %x",__FILE__,__LINE__,beHeader.length);
	beHeader.length =htons(beHeader.length);
//	CWLog("[F:%s, L:%d] :beHeader.length = %x",__FILE__,__LINE__,beHeader.length);
	//beHeader.timestamp =time;
	//beHeader.type = BE_CAPWAP_HEADER;
	//beHeader.length =*len + MAC_ADDR_LEN;

	if(!CWErr(CWThreadMutexLock(&gWTPsMutex))) {
		CWLog("Error locking the gWTPsMutex mutex");
		return CW_FALSE;
	}
	
	if(gWTPs[apId].isNotFree && (gWTPs[apId].currentState == CW_ENTER_RUN))
	{
		for(i=0; i<MAC_ADDR_LEN; i++)
		{
			beHeader.apMac[i] =  gWTPs[apId].MAC[i];
		}
	}
	CWThreadMutexUnlock(&gWTPsMutex);
	
	CWLog("[F:%s, L:%d] :beHeader.mac = %x:%x:%x:%x:%x:%x",__FILE__,__LINE__,beHeader.apMac[0],beHeader.apMac[1],beHeader.apMac[2],beHeader.apMac[3],beHeader.apMac[4],beHeader.apMac[5]);
	CW_CREATE_STRING_ERR(rsp, packetLen+1, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});				
	memset(rsp, 0, packetLen+1);
	//�ֽڶ���
	memcpy(rsp,(char*)&beHeader, BE_TYPELEN_LEN+TIME_LEN+MAC_ADDR_LEN);
	memcpy(rsp+BE_TYPELEN_LEN+TIME_LEN+MAC_ADDR_LEN,buf, *len);
	
	CWLog("[F:%s, L:%d] :buf len = %d",__FILE__,__LINE__,*len);
	*len = packetLen;
	
	//CWLog("[F:%s, L:%d] :rsp = %s",__FILE__,__LINE__,rsp);	
	CWLog("[F:%s, L:%d] :packetLen = %d",__FILE__,__LINE__,*len);
	
	return rsp;
}

void SendBEResponse(char* buf,int len,int apId)
{
	int n,socketIndex;
	n = 0;

	if(!CWErr(CWThreadMutexLock(&gWTPsMutex))) {
		CWLog("Error locking the gWTPsMutex mutex");
		return;
	}
	socketIndex = gWTPs[apId].applicationIndex;

	CWThreadMutexUnlock(&gWTPsMutex);
	
	if(!CWErr(CWThreadMutexLock(&appsManager.socketMutex[socketIndex]))) {
		CWLog("Error locking numSocketFree Mutex");
		return;
	}
	
	while(n != len)
	{
		if ( (n += Writen(appsManager.appSocket[socketIndex], buf, len))  < 0 ) {
			CWThreadMutexUnlock(&appsManager.socketMutex[socketIndex]);
			CWLog("Error locking numSocketFree Mutex");
			return;
		}
		CWLog("[F:%s, L:%d] Writen n:%d",__FILE__,__LINE__,n);
	}
	CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
	CWThreadMutexUnlock(&appsManager.socketMutex[socketIndex]);

}


void SendBERequest(char* buf,int len)
{
	int ret,n;
//	struct sockaddr_in servaddr;
/*
	char *address = gACBEServerAddr;
	int port = gACBEServerPort;
	
	CWLog("SendBERequest ,addr = %s,port = %d...... ",address,port);
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		CWLog("SendBERequest socket init error ");
		close(sockfd);
		return ;
	}

	bzero(&servaddr, sizeof (struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	inet_pton(AF_INET, address, &servaddr.sin_addr);

	//flags = fcntl(sockfd,F_GETFL,0);
	//fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	//fcntl(sockfd, F_SETFL, 0);
*/

	if(!CWErr(CWThreadMutexLock(&appsManager.appClientSocketMutex))) {
				CWLog("Error locking numSocketFree Mutex");
				return NULL;
			}

#if 0
	if ((ret = connect(appsManager.appClientSocket, (SA*) &appsManager.appClientAddr, sizeof(struct sockaddr_in)) )< 0) {
		CWLog("SendBERequest connect error,ret = %d",ret);
		CWThreadMutexUnlock(&appsManager.appClientSocketMutex);
		return ;
	}
#endif
	CWLog("[F:%s, L:%d] SendBERequset len:%d",__FILE__,__LINE__,len);
	while(n != len)
	{
		if ( (n += Writen(appsManager.appClientSocket, buf, len))  < 0 ) {
			continue;
		}
		CWLog("[F:%s, L:%d] Writen n:%d !=len",__FILE__,__LINE__,n);
	}
	CWLog("[F:%s, L:%d] Writen n:%d",__FILE__,__LINE__,n);
	CWThreadMutexUnlock(&appsManager.appClientSocketMutex);
	n = 0;
	ret = 0;
/*
	if (close(sockfd) < 0) {
		CWLog("SendBERequest close error");
	}
	*/
/*
	if (Read32(sockfd, &ret) != 4) {
		exit(1);
	}

	if (ret == -1) {
		fprintf(stderr, "The AC Server's Client Queue Is Currently Full.\n");
		exit(1);
	} else if (ret != 1) {
		fprintf(stderr, "Something Wrong Happened While Connecting To The AC Server.\n");
		exit(1);
	}	
*/
	return;
}


/************************************************************************
 * CWOFDMSetValues provide to set the command values (type, parameters,	*
 * output socket) on the correct wtp structure.							*
 ************************************************************************/
int CWXMLSetValues(int selection, int socketIndex, CWVendorXMLValues* xmlValues) {
//CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);	
	CWThreadMutexLock(&(gWTPs[selection].interfaceMutex));
	//Free Old	
	CW_CREATE_OBJECT_ERR(gWTPs[selection].vendorValues, CWProtocolVendorSpecificValues, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});
	gWTPs[selection].vendorValues->vendorPayloadLen = 0;
	
	//CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
	
	if(xmlValues->payloadLen && xmlValues->payload)
	{
		CW_CREATE_STRING_ERR(gWTPs[selection].vendorValues->payload, xmlValues->payloadLen+1, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});
		memset(gWTPs[selection].vendorValues->payload,0,xmlValues->payloadLen+1);
		memcpy((char*)gWTPs[selection].vendorValues->payload, xmlValues->payload,xmlValues->payloadLen);
		CWLog("gWTPs[%d].vendorValues->payload :%s", selection, gWTPs[selection].vendorValues->payload);
		CWLog("gWTPs[%d].vendorValues->payload Len:%d", selection, strlen(gWTPs[selection].vendorValues->payload));
		gWTPs[selection].vendorValues->vendorPayloadLen = strlen(gWTPs[selection].vendorValues->payload);
	}
	if(xmlValues->wum_type == WTP_CONFIG_REQUEST)
	{
		CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
		gWTPs[selection].vendorValues->vendorPayloadType = CW_MSG_ELEMENT_VENDOR_SPEC_PAYLOAD_CONFIG;
		gWTPs[selection].interfaceCommand = WTP_CONFIG_CMD;
	}
	else if(xmlValues->wum_type == WTP_STATE_REQUEST)
	{
		CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
		gWTPs[selection].vendorValues->vendorPayloadType = CW_MSG_ELEMENT_VENDOR_SPEC_PAYLOAD_STATE;
		gWTPs[selection].interfaceCommand = WTP_STATE_CMD;
	}
	else
	{
		CWLog("[F:%s, L:%d]  Unknown wum_type:%d",__FILE__,__LINE__,xmlValues->wum_type);
		return FALSE;
	}
	CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
	gWTPs[selection].applicationIndex = socketIndex;
	CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
	CWSignalThreadCondition(&gWTPs[selection].interfaceWait);
	CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
	//block
	CWWaitThreadCondition(&gWTPs[selection].interfaceComplete, &gWTPs[selection].interfaceMutex);
	CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);
	CWThreadMutexUnlock(&(gWTPs[selection].interfaceMutex));
	CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);	
	return TRUE;
}


int CWWumSetValues(int selection, int socketIndex, CWProtocolVendorSpecificValues* vendorValues) {
	
	CWThreadMutexLock(&(gWTPs[selection].interfaceMutex));
	
	gWTPs[selection].vendorValues = vendorValues;
	gWTPs[selection].interfaceCommand = WTP_UPDATE_CMD;
	gWTPs[selection].applicationIndex = socketIndex;
	CWSignalThreadCondition(&gWTPs[selection].interfaceWait);
	CWWaitThreadCondition(&gWTPs[selection].interfaceComplete, &gWTPs[selection].interfaceMutex);
	
	CWThreadMutexUnlock(&(gWTPs[selection].interfaceMutex));
	
	return 0;
}	


/************************************************************************
 * CWManageApplication is the function that provide the management of	*
 * interaction with a single application.								*
 * -------------------------------------------------------------------- *
 * The messages used are defined in ACAppsProtocol.h					*
 ************************************************************************/

CW_THREAD_RETURN_TYPE CWManageApplication(void* arg) {
	
	int socketIndex = ((CWInterfaceThreadArg*)arg)->index;
	CWSocket sock = appsManager.appSocket[socketIndex];
	int n, connected= htonl(CONNECTION_OK), gActiveWTPsTemp;
	
	char commandBuffer[COMMAND_BUFFER_SIZE];
	char wtpListBuffer[WTP_LIST_BUFFER_SIZE];
	
	int payload_size;
	int i, j,k, nameLength, numActiveWTPs=0, wtpIndex;
   	int iTosend, nLtoSend;
	unsigned char msg_elem;
	unsigned short beType,beLen;
	BEHeader beHeader;
	CWProtocolVendorSpecificValues* vendorValues;
	CWVendorWumValues* wumValues;
	CWVendorXMLValues* xmlValues;
	char result,macTemp[MAC_ADDR_LEN];
	//test
#if 0
	int BESize,resultCode = 0,WTPIndex = 0,payloadSize;
	char *beResp = NULL;
#endif
		
	/********************************************************************************
	 * Write on application socket that connection setting is happened correctly.	*
	 ********************************************************************************/
  	/*
	if ( Writen(sock, &connected, sizeof(int) ) < 0 ) {
		CWLog("Error on writing on application socket ");
		return NULL;
	}
*/
	/*
	 * Before starting, make sure to detach the thread because the parent 
 	 * doesn't do a join and we risk resource leaks.
 	 *
 	 * ref -> BUG-TRL01
	 * 15/10/2009 - Donato Capitella
	 */

        pthread_detach(pthread_self()); // no need here to check return value
	
	/************************
	 *	 Thread Main Loop	*
	 ************************/
	
	CW_REPEAT_FOREVER 
	{ 

		memset(commandBuffer, 0, COMMAND_BUFFER_SIZE);
		
		/****************************************
		 *		Waiting for client commands		*
		 ****************************************/

		//Parse BEHeader
		n = 0;
		beType = 0;
		beLen = 0;
		beHeader.type = 0;
		
		if ( ( n = Readn(sock, &beHeader.type, BE_TYPE_SIZE))> 0 ) 
		{
			//type
			beHeader.type = ntohs(beHeader.type );
			if(beHeader.type != BE_CAPWAP_HEADER)
			{
				CWLog("Error on receive BEHeader !,type = %d",beHeader.type);
				goto quit_manage;
				//continue;
			}
			CWLog("Receive BEHeader ...");

			//len
			if  ((n = Readn(sock, &beHeader.length, BE_LENGTH_SIZE)) < 0 )
			{
					CWLog("Error while reading from socket.");
					goto quit_manage;
					//continue;
			}
			beHeader.length = ntohs(beHeader.length);
			if(beHeader.length < BE_HEADER_MIN_LEN )
			{
				CWLog("Error beHeader.length = %d not in range !",beHeader.length);
				goto quit_manage;
				//continue;
			}	
			CWLog("Receive BEHeader.length = %d",beHeader.length);

			//timestamp
			if  ((n = Readn(sock, &beHeader.timestamp, TIME_LEN)) < 0 )
			{
					CWLog("Error while reading from socket.");
					goto quit_manage;
					//continue;
			}
			beHeader.timestamp = Swap32(beHeader.timestamp);
			
			CWLog("Receive BEHeader.timestamp = %d",beHeader.timestamp);

			//apmac
			if ((n = Readn(sock, beHeader.apMac, MAC_ADDR_LEN )) < 0 ) 
			{
				CWLog("Error while reading from socket.");
				goto quit_manage;
				//continue;
			}
#if 0
			memset(macTemp,0,MAC_ADDR_LEN);
			memcpy(macTemp,beHeader.apMac,MAC_ADDR_LEN);
			
			for(i = (MAC_ADDR_LEN -1);i >= 0;i--)
			{
				j = MAC_ADDR_LEN -1 -i;
				beHeader.apMac[j] = macTemp[i];				
			}
#endif			
			CWLog("Receive beHeader.apMac = %x:%x:%x:%x:%x:%x ",
								beHeader.apMac[0],
								beHeader.apMac[1],
								beHeader.apMac[2],
								beHeader.apMac[3],
								beHeader.apMac[4],
							   	beHeader.apMac[5]);


			//test
#if 0
			//BE: ap connect
			BEconnectEvent beConEve;
			int BESize;
			char *beResp = NULL;
			beConEve.type = htons(BE_CONNECT_EVENT);
			beConEve.length = htons(BE_CONNECT_EVENT_LEN);
			CWLog("[F:%s, L:%d] :-------------sendpacket(start)-------------------------",__FILE__,__LINE__);
			CWLog("[F:%s, L:%d] :connectevent type=%d,length=%d,state=%d",__FILE__,__LINE__,BE_CONNECT_EVENT,BE_CONNECT_EVENT_LEN,BE_CONNECT_EVENT_CONNECT);
			//beConEve.type = BE_CONNECT_EVENT;
			//beConEve.length = BE_CONNECT_EVENT_LEN;
			beConEve.state = BE_CONNECT_EVENT_CONNECT;
			BESize = BE_CONNECT_EVENT_LEN + BE_TYPELEN_LEN;
			wtpIndex = 0;
			
			gWTPs[wtpIndex].applicationIndex = socketIndex;
			gWTPs[wtpIndex].isNotFree = TRUE;
			gWTPs[wtpIndex].currentState = CW_ENTER_RUN;
			
			for(i=0; i<MAC_ADDR_LEN; i++)
			{
				gWTPs[wtpIndex].MAC[i] = 88;
				//CWLog("[F:%s, L:%d] :i=%d,wtpIndex= %d,gWTPs[wtpIndex].MAC[i] = %x",__FILE__,__LINE__,i,wtpIndex,gWTPs[wtpIndex].MAC[i]);	
			}
		
			beResp = AssembleBEheader((char*)&beConEve,&BESize,wtpIndex);
			CWLog("[F:%s, L:%d] :BESize = %d",__FILE__,__LINE__,BESize);
			if(beResp)
			{
				//SendBERequest(beResp,BESize);
				SendBEResponse(beResp,BESize,wtpIndex);
			CWLog("[F:%s, L:%d] :-------------sendpacket(end)-------------------------",__FILE__,__LINE__);
				CW_FREE_OBJECT(beResp);
			}
			else
			{
				CWLog("Error AssembleBEheader !");
				goto quit_manage;
			}
#endif	
			//				
		}
		//BEHeader end
		else
		{
			CWLog("Receive packet beHeader.type = %d is not BE, drop it !",beHeader.type);
			goto quit_manage;
			//continue;
		}
		if ( ( n = Readn(sock, &beType, BE_TYPE_SIZE) ) > 0 ) 
		{
			beType = ntohs(beType);
			
			if ( beType == BE_MONITOR_EVENT_REQUSET )
			{
				CWLog("Receive BE_MONITOR_EVENT_REQUSET !");
				
				//len
				if ( (n = Readn(sock, &beLen, BE_LENGTH_SIZE) )< 0 )
				{
						CWLog("Error while reading from socket.");
						goto quit_manage;
						//continue;
				}
				beLen = ntohs(beLen);
				if(beLen)
				{
					CWLog("Error beLen = %d not in range !",beLen);
					goto quit_manage;
					//continue;
				}	
				CWLog("Receive beLen = %d",beLen);

#if 0
//test
				char *temp = NULL;
				temp = "<?xml version=\"1.0\"?><config><SSID>1</SSID></config>";
				
				payloadSize = strlen(temp);
			       BEmonitorEventResponse beMonitorEventResp;
				beMonitorEventResp.type =htons( BE_MONITOR_EVENT_RESPONSE) ;
				beMonitorEventResp.length = htons(payloadSize);
				
				CW_CREATE_STRING_ERR(beMonitorEventResp.xml, payloadSize, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});				
				memset(beMonitorEventResp.xml, 0, payloadSize);
				memcpy(beMonitorEventResp.xml, "12345678", payloadSize);
				BESize = BE_TYPELEN_LEN+payloadSize;

				beResp = AssembleBEheader((char*)&beMonitorEventResp,&BESize,WTPIndex);
				CW_FREE_OBJECT(beMonitorEventResp.xml);

				if(beResp)
				{
					//SendBERequest(beResp,BESize);
					SendBEResponse(beResp,BESize,wtpIndex);
					CW_FREE_OBJECT(beResp);
				}
				else
				{
					CWLog("Error AssembleBEheader !");
					goto quit_manage;
				}	
#endif

				CW_CREATE_OBJECT_ERR(xmlValues, CWVendorXMLValues, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});
				xmlValues->wum_type =WTP_STATE_REQUEST;
				xmlValues->payloadLen = beLen;
				CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);	

				result = FALSE;
				result = BESetApValues(beHeader.apMac, socketIndex, xmlValues);
				//CW_FREE_OBJECT(xmlValues->payload);
				CW_FREE_OBJECT(xmlValues);
				CWLog("[F:%s, L:%d] ",__FILE__,__LINE__);	
				if(!result)
				{
					goto quit_manage;
					//continue;
				}


			}

			if ( beType == BE_CONFIG_EVENT_REQUSET ) {

				/****************************************
				 * Manage CONF command			*
				 * ------------------------------------ *
				 * 1. Select the type of CONF_UPDATE,	*
				 * 2. Get Index of WTP,			*
				 * 3. Manage request.			*
				 ****************************************/
				CWLog("Receive BE_CONFIG_EVENT_REQUSET !");
				
				//len
				if ( (n = Readn(sock, &beLen, BE_LENGTH_SIZE) )< 0 ) {
						CWLog("Error while reading from socket.");
						goto quit_manage;
						//continue;
				}
				beLen = ntohs(beLen);
				if(!beLen)
				{
					CWLog("Error beLen = %d not in range !",beLen);
					goto quit_manage;
					//continue;
				}	
				CWLog("Receive beLen = %d",beLen);
//test
#if 0
				BEconfigEventResponse beConfigEventResp;
				beConfigEventResp.type = htons(BE_CONFIG_EVENT_RESPONSE) ;
				// 4 sizeof(int)
				beConfigEventResp.length = htons(sizeof(resultCode));//4
				beConfigEventResp.resltCode = Swap32(resultCode);

				//CW_CREATE_STRING_ERR(&beConfigEventResp.resltCode, payloadSize, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});				
				//memset(beMonitorEventResp.xml, 0, payloadSize);
				//memcpy(beMonitorEventResp.xml, vendValues->payload, payloadSize);
				payloadSize = 4;
				BESize = BE_TYPELEN_LEN+payloadSize;
			
				beResp = AssembleBEheader((char*)&beConfigEventResp,&BESize,WTPIndex);

				if(beResp)
				{
					//SendBERequest(beResp,BESize);
					SendBEResponse(beResp,BESize,wtpIndex);
					CW_FREE_OBJECT(beResp);
				}
				else
				{
					CWLog("Error AssembleBEheader !");
					goto quit_manage;
				}	
#endif	
				CW_CREATE_OBJECT_ERR(xmlValues, CWVendorXMLValues, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});
				xmlValues->wum_type =WTP_CONFIG_REQUEST;
				xmlValues->payloadLen = beLen;
				CW_CREATE_STRING_ERR(xmlValues->payload, beLen+1, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});
				memset(xmlValues->payload,0,beLen+1);

				if ( (n = Readn(sock, xmlValues->payload, beLen) )< 0 ) {
						CWLog("Error while reading from socket.");
						goto quit_manage;
						//continue;
						
				}
				
				if(!xmlValues->payload){
					CWLog("Error xml= %s is NULL !",xmlValues->payload);
					goto quit_manage;
					//continue;
				}	
				
				CWLog("Receive xml len =%d,value = %s",n, xmlValues->payload);

				result = FALSE;
				result = BESetApValues(beHeader.apMac, socketIndex, xmlValues);
				CW_FREE_OBJECT(xmlValues->payload);
				CW_FREE_OBJECT(xmlValues);	
				if(!result)
				{
					goto quit_manage;
					//continue;
				}
				
			
			}

			else
			{
				CWLog("BE body type = %d, unknown!",beType);
			}
		}
		else
		{
			CWLog("BE type exist,but no body !");
			goto quit_manage;
			//continue;
		}
	}
	
quit_manage:

	if(!CWErr(CWThreadMutexLock(&appsManager.numSocketFreeMutex))) {
		CWLog("Error locking numSocketFree Mutex");
		return NULL;
	}
	
	appsManager.isFree[socketIndex] = CW_TRUE;
	appsManager.numSocketFree++;
	
       CWDestroyThreadMutex(&appsManager.socketMutex[socketIndex]);

	CWThreadMutexUnlock(&appsManager.numSocketFreeMutex); 
		
	close(sock);
	return NULL;  
}

/****************************************************************************
 * CWInterface is the function that provide the interaction between AC and	*
 * extern applications. Listen on main Inet familty socket and create a		*
 * CWManageApplication thread for every client connected.					*
 ****************************************************************************/

CW_THREAD_RETURN_TYPE CWInterface(void* arg)
{
	 
	CWSocket listen_sock, conn_sock;
	struct sockaddr_in servaddr;
	CWInterfaceThreadArg *argPtr;
	CWThread thread_id;
	int i, clientFull = htonl(FULL_CLIENT_CONNECTED), optValue = 1;
	
	/****************************************************
	 * Setup of Application Socket Management Structure	*
	 ****************************************************/
	 /* BE client */
	
	int clientSock, ret;
	struct sockaddr_in clientAddr;
	char *clientAddress = gACBEServerAddr;
	int clientPort = gACBEServerPort;
	
	CWLog("clientSock ,addr = %s,port = %d...... ",clientAddress,clientPort);
	if ((clientSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		CWLog("clientSock init error ");
		close(clientSock);
		return NULL;
	}

	bzero(&clientAddr, sizeof (struct sockaddr_in));
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_port = htons(clientPort);
	inet_pton(AF_INET, clientAddress, &clientAddr.sin_addr);

	//flags = fcntl(sockfd,F_GETFL,0);
	//fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	//fcntl(sockfd, F_SETFL, 0);

	if ((ret = connect(clientSock, (SA*) &clientAddr, sizeof(struct sockaddr_in)) )< 0) {
		CWLog("clientSock connect error,ret = %d",ret);
		//test
		close(clientSock);
		return NULL;
	}

	if ( !CWErr(CWCreateThreadMutex(&appsManager.appClientSocketMutex)) ) {
		CWLog("Error on mutex creation on appManager structure: appClientSocketMutex");
		return NULL;
	}

	if(!CWErr(CWThreadMutexLock(&appsManager.appClientSocketMutex))) {
				CWLog("Error locking numSocketFree Mutex");
				return NULL;
			}
	appsManager.appClientSocket = clientSock;
	bzero(&appsManager.appClientAddr, sizeof (struct sockaddr_in));
	memcpy(&appsManager.appClientAddr,&clientAddr,sizeof (struct sockaddr_in));

	CWThreadMutexUnlock(&appsManager.appClientSocketMutex);

	
	//
	for ( i=0; i < MAX_APPS_CONNECTED_TO_AC; i++) 
		appsManager.isFree[i] = CW_TRUE;	
	
	appsManager.numSocketFree = MAX_APPS_CONNECTED_TO_AC;
	
	if ( !CWErr(CWCreateThreadMutex(&appsManager.numSocketFreeMutex)) ) {
		CWLog("Error on mutex creation on appManager structure");
		return NULL;
	}
		
	/****************************************************
	 * Setup (Creation and filling) of main socket		*
	 ****************************************************/
		
	if ( ( listen_sock = socket(AF_INET, SOCK_STREAM, 0 ) ) < 0 ) {
		CWLog("Error on socket creation on Interface");
		return NULL;
	}
	
	memset(&servaddr, 0, sizeof(servaddr));
	
	servaddr.sin_family = AF_INET;
	//servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* Not Extern: INADDR_ANY */
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Not Extern: INADDR_ANY */
	servaddr.sin_port = htons(LISTEN_PORT); 

	if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optValue, sizeof(int)) == -1) {
		CWLog("Error on socket creation on Interface");
		return NULL;
	}
	
	/************************************
	 * Binding socket and Listen call	*
	 ************************************/
	
	if (  bind(listen_sock, (struct sockaddr *) &servaddr, sizeof(struct sockaddr_in)) < 0 ) {
		CWLog("Error on Binding");
		return NULL;
	}
	
	if ( listen(listen_sock, MAX_APPS_CONNECTED_TO_AC) < 0 ) {
		CWLog("Error on LIsTsocket creation");
		return NULL;
	}
	
	/********************************
	 *			Main Loop			*
	 ********************************/
	
	
	CW_REPEAT_FOREVER
      {
		if ( ( conn_sock = accept(listen_sock, (struct sockaddr *) NULL, NULL) ) > 0 ) { 
			
			/************************************************************************	
			 * Check (with lock) the number of socket free at the moment of accept,	*
			 * if this value is greater than 0 will be spawn a new Manage thread.	*
			 ************************************************************************/
			
			if(!CWErr(CWThreadMutexLock(&appsManager.numSocketFreeMutex))) {
				CWLog("Error locking numSocketFree Mutex");
				return NULL;
			}
			
			if ( appsManager.numSocketFree > 0 ) { 
				
				CW_CREATE_OBJECT_ERR(argPtr, CWInterfaceThreadArg, {CWErrorRaise(CW_ERROR_OUT_OF_MEMORY, NULL); return 0;});
				
				/************************************
				 *	Search socket for application	*
				 ************************************/
					
				for (i=0; i < MAX_APPS_CONNECTED_TO_AC; i++) 
	                   {                  
					if ( appsManager.isFree[i] == CW_TRUE ) {
						argPtr->index = i;
						appsManager.isFree[i] = CW_FALSE;
						appsManager.appSocket[i] = conn_sock;				  
						break;
					}
	                    }
					
				appsManager.numSocketFree--;
				CWThreadMutexUnlock(&appsManager.numSocketFreeMutex);
				
				if ( !CWErr(CWCreateThreadMutex(&appsManager.socketMutex[argPtr->index])) ) {
	              			CWLog("Error on mutex creation on appManager structure");
	              			return NULL;
				}
							
				if(!CWErr(CWCreateThread(&thread_id, CWManageApplication, argPtr))) {
					CWLog("Error on thread CWManageApplication creation");
					appsManager.isFree[argPtr->index] = CW_TRUE;
					close(conn_sock);
					CW_FREE_OBJECT(argPtr);
	                                    /* 
	                                     * If we can't create the thread, we have to increment numSocketFree.
	                                     *
	                                     *   ref -> BUG-LE01
	                                     *   15/10/2009 - Donato Capitella
	                                     */
	                                    if(!CWErr(CWThreadMutexLock(&appsManager.numSocketFreeMutex))) {
	                                            CWLog("Error locking numSocketFree Mutex");
	                                            return NULL;
	                                    }
	                                    appsManager.numSocketFree++;
	                                    CWThreadMutexUnlock(&appsManager.numSocketFreeMutex);

				}
			}
			else {
				CWThreadMutexUnlock(&appsManager.numSocketFreeMutex);
			
				/****************************************************************
				 *	There isn't space for another client, send error answer.	*
				 ***************************************************************/
				
				/*if ( Writen(conn_sock, &clientFull, sizeof(int) ) < 0 ) {
					printf("Error on sending Error Message\n");
					close(conn_sock);
				}*/
			}		  
		}
		else
			CWLog("Error on accept (applications) system call");
      }
	
	CWDestroyThreadMutex(&appsManager.numSocketFreeMutex);
	CWDestroyThreadMutex(&appsManager.appClientSocketMutex);
	
	close(listen_sock);
}


int is_valid_wtp_index(int wtpIndex) 
{
	if (wtpIndex < CW_MAX_WTP && gWTPs[wtpIndex].isNotFree)
		return CW_TRUE;
	return CW_FALSE;
}

/*
 * Steven's readn().
 */
int readn(int fd, void *vptr, size_t n)
{
	size_t	nleft;
	ssize_t	nread;
	char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = recv(fd, ptr, nleft, 0)) < 0) {
			if (errno == EINTR)
				nread = 0;		/* and call read() again */
			else
				return(-1);
		} else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

int Readn(int fd, void *ptr, size_t nbytes)
{
	int n;

	if ( (n = readn(fd, ptr, nbytes)) < 0) {
		CWLog("Error while reading data from socket.");
		return -1;
	}

	return n;
}
			
