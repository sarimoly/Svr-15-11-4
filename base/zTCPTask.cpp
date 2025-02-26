/**
 * \file
 * \version  $Id: zTCPTask.cpp  $
 * \author  
 * \date 
 * \brief 实现线程池类，用于处理多连接服务器
 *
 * 
 */


#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <assert.h>

#include "zSocket.h"
#include "zThread.h"
#include "zTCPTask.h"
#include "zTCPTaskPool.h"
#include "Zebra.h"

CmdAnalysis zTCPTask::analysis("Task指令接收统计",600);
/**
 * \brief 向套接口发送指令，如果缓冲标志设置，则发送是直接拷贝到缓冲区队列中，实际的发送动作在另外一个线程做
 *
 *
 * \param pstrCmd 待发送的指令
 * \param nCmdLen 待发送指令的大小
 * \return 发送是否成功
 */
bool zTCPTask::sendCmd(const void *pstrCmd, int nCmdLen)
{
	//Zebra::logger->trace("zTCPTask::sendCmd");	
	//static CmdAnalysis analysis("Task指令发送统计",600);
	/*
	Cmd::t_NullCmd *ptNullCmd = (Cmd::t_NullCmd *)pstrCmd;
	analysis.add(ptNullCmd->cmd,ptNullCmd->para,nCmdLen);
	// */
	return mSocket.sendCmd(pstrCmd, nCmdLen, buffered);
}

bool zTCPTask::sendCmdNoPack(const void *pstrCmd, int nCmdLen)
{
	//Zebra::logger->trace("zTCPTask::sendCmdNoPack");
	return mSocket.sendCmdNoPack(pstrCmd, nCmdLen, buffered);
}

/**
 * \brief 从套接口中接受数据，并且拆包进行处理，在调用这个函数之前保证已经对套接口进行了轮询
 *
 * \param needRecv 是否需要真正从套接口接受数据，false则不需要接收，只是处理缓冲中剩余的指令，true需要实际接收数据，然后才处理
 * \return 接收是否成功，true表示接收成功，false表示接收失败，可能需要断开连接 
 */
bool zTCPTask::ListeningRecv(bool needRecv)
{
	//Zebra::logger->trace("zTCPTask::ListeningRecv");
	int retcode = 0;
	if (needRecv) {
		retcode = mSocket.recvToBuf_NoPoll();
	}
	//struct timeval tv_2;
	if (-1 == retcode)
	{		
		return false;
	}
	else
	{
		do
		{
			unsigned char pstrCmd[zSocket::MAX_DATASIZE];
			int nCmdLen = mSocket.recvToCmd_NoPoll(pstrCmd, sizeof(pstrCmd));
			if (nCmdLen <= 0)
				//这里只是从缓冲取数据包，所以不会出错，没有数据直接返回
				break;
			else
			{
				Cmd::t_NullCmd *ptNullCmd = (Cmd::t_NullCmd *)pstrCmd;
				if (Cmd::CMD_NULL == ptNullCmd->cmd
						&& Cmd::PARA_NULL == ptNullCmd->para)
				{
					//返回的测试指令，需要递减计数
					//Zebra::logger->debug("服务端收到返回测试信号");
					clearTick();
				}
				else
				{
					msgParse(ptNullCmd, nCmdLen);
					/*
					analysis.add(ptNullCmd->cmd,ptNullCmd->para,nCmdLen);
					// */
				}
			}
		}
		while(true);
	}

	return true;
}

/**
 * \brief 发送缓冲中的数据到套接口，再调用这个之前保证已经对套接口进行了轮询
 *
 * \return 发送是否成功，true表示发送成功，false表示发送失败，可能需要断开连接
 */
bool zTCPTask::ListeningSend()
{
	//Zebra::logger->trace("zTCPTask::ListeningSend");
	return mSocket.sync();
}

/**
 * \brief 把TCP连接任务交给下一个任务队列，切换状态
 *
 */
void zTCPTask::getNextState()
{
	//Zebra::logger->trace("zTCPTask::getNextState()");
	zTCPTask_State old_state = getState();

	switch(old_state)
	{
		case notuse:
			setState(verify);
			break;
		case verify:
			setState(sync);
			break;
		case sync:
			buffered = true;
			addToContainer();
			setState(okay);
			break;
		case okay:
			removeFromContainer();
			setState(recycle);
			break;
		case recycle:
			setState(notuse);
			break;
	}

	Zebra::logger->debug("%s(%s:%u), %s -> %s)", __FUNCTION__,  getIP(), getPort(),getStateString(old_state), getStateString(getState()));
}

/**
 * \brief 重值连接任务状态，回收连接
 *
 */
void zTCPTask::resetState()
{
	//Zebra::logger->trace("zTCPTask::resetState");
	zTCPTask_State old_state = getState();

	switch(old_state)
	{
		case notuse:
		/*
		 * whj 
		 * 如果sync情况下添加到okay管理器失败会出现okay状态resetState的可能性
		 */
		//case okay:
		case recycle:
			//不可能的
			Zebra::logger->fatal("%s:不可能 recycle -> recycle", __FUNCTION__);
			break;
		case verify:
		case sync:
		case okay:
			//TODO 相同的处理方式
			break;
	}

	setState(recycle);
	Zebra::logger->debug("%s(%s:%u), %s -> %s)", __FUNCTION__,  getIP(),getPort(), getStateString(old_state), getStateString(getState()));
}

void zTCPTask::checkSignal(const zRTime &ct)
{
	///Zebra::logger->trace("zTCPTask::checkSignal");
	if (ifCheckSignal() && checkInterval(ct))
	{
		if (checkTick())
		{
			//测试信号在指定时间范围内没有返回
			Zebra::logger->error("套接口检查测试信号失败");
			Terminate(zTCPTask::terminate_active);
		}
		else
		{
			//发送测试信号
			Cmd::t_NullCmd tNullCmd;
			//Zebra::logger->debug("服务端发送测试信号");
			if (sendCmd(&tNullCmd, sizeof(tNullCmd)))
				setTick();
		}
	}
}

