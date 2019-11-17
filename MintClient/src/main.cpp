#include "mtpch.h"
#include "MTLibrary/Packet.h"
#include "MTLibrary/Logger.h"
#include "MTLibrary/MemoryCounter.h"
#include "MTLibrary/MemoryPool.h"
#include "MTLibrary/Task.h"

#define SERVER_PORT    27015

struct SocketClient
{
	SOCKET    Socket;
	char      Name[NAME_BUFFER_MAX];
	WORD      S2C_PacketOrderNumber;
	WORD      C2S_PacketOrderNumber;
	TaskNode* pAliveNode;
};

enum _TASK_TYPE_
{
	TASK__ALIVE = 1,
};

class TCPClient : public TaskInterface
{
	SocketClient    Client;
	PacketBuffer    RecvPacket;
	int             RecvPacketLength;
	int             RecvPacketBufferIndex;

	TaskComponent* TaskManager;

public:
	TCPClient()
		:TaskManager(NULL), RecvPacketLength(0), RecvPacketBufferIndex(0), Client({ 0 }), RecvPacket({0}) {} // class�۰ʪ�l�Ƭ�0
	
	bool Create()
	{
		if (CreateWinsock() == false)
			return false;

		RecvPacketLength = PACKET_LENGTH_SIZE;

		TaskManager = TaskComponent::sCreate(this);
		Client.pAliveNode = TaskManager->vAddTask(TASK__ALIVE, 1000 * 30, TASK_RUN_COUNT_UNLIMITED);

		return true;
	}
	bool CreateWinsock()
	{
		printf("TCP client create\n");

		WSADATA wsaData = { 0 };
		int result = 0;

		// ��l�� winsock�A�ϥ�2.2����
		result = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (result != 0)
		{
			printf("WSAStartup failed, error: %d\n", result);
			return false;
		}

		// �Ыؤ@�ӳs�u��client socket
		// AF_INET : �ϥ�IPv4 ��ĳ
		// SOCK_STREAM : �@�ӧǦC�ƪ��s���ɦV�줸�y�A�i�H���줸�y�ǿ�C������protocol��TCP�C
		// IPPROTO_TCP : �ϥ� TCP ��ĳ�C�Τ]�i��JNULL�A��ܨϥΨt�ιw�]����ĳ�A�YTCP��ĳ�C
		Client.Socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Client.Socket == INVALID_SOCKET)
		{
			printf("socket failed, error: %d\n", ::WSAGetLastError());
			return false;
		}

		const char* ip = "127.0.0.1";             // �s�쥻����server ip
		sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;                 // �ϥ�IPv4 ��ĳ
		addr.sin_port = htons(SERVER_PORT);    // �]�wport
		::inet_pton(AF_INET, ip, &addr.sin_addr); // �]�wIP�Aclient�s�u��server��

		result = ::connect(Client.Socket, (SOCKADDR*)&addr, sizeof(addr)); // connect�s�u��server...
		if (result == SOCKET_ERROR)
		{
			printf("connect failed, error: %d\n", ::WSAGetLastError());
			return false;
		}

		if (Client.Socket == INVALID_SOCKET)
		{
			printf("unable to connect to server !\n");
			return false;
		}
		else
			printf("connect to server !\n");

		// ��ConnectSocket�]���D����Ҧ��]non-blocking�^
		// FIONBIO : �]�w�βM���D����Ҧ�
		unsigned long non_blocking_mode = 1; // �n�ҥΫD����Ҧ�, �ҥH�]��1
		result = ::ioctlsocket(Client.Socket, FIONBIO, &non_blocking_mode);
		if (result == SOCKET_ERROR)
		{
			printf("ioctlsocket failed, error: %d", ::WSAGetLastError());
			return false;
		}

		const char optval_enable = 1;
		int ret = ::setsockopt(Client.Socket, IPPROTO_TCP, TCP_NODELAY, &optval_enable, sizeof(optval_enable));
		if (ret == -1)
		{
			printf("setsockopt( TCP_NODELAY ) failed, error: %d", ::WSAGetLastError());
		}

		return true;
	}

	void NameInput()
	{
		// ��J�ʺ٦r��

		int name_length = 0;

		while (1)
		{
			printf("name input : ");
			::fgets(Client.Name, NAME_BUFFER_MAX, stdin); // fgets()�|���ݱq��L��J�r��
			if (Client.Name[0] == '\n')
				continue;

			name_length = ::strlen(Client.Name); // ���o�r�����
			if (name_length > 0)
			{
				Client.Name[name_length - 1] = 0; // �]���ϥ�fgets()�A�����|���@�� '\n' �Ÿ��A���R��.
				name_length -= 1;
				break;
			}
		}

		Send_C2S_Name(Client.Name, name_length);
	}

	void ChatInput()
	{
		// ��ѥD�j��

		int  keyboard_state = 0, ch = 0;
		int  buff_message_length = 0;
		char buff_message[PACKET_BUFFER_MAX] = { 0 };

#define BACKSPCAE_KEY_CODE  8  // Backspace�˰h��X
#define ENTER_KEY_CODE     13  // Enter��X

		printf("chat input... \n");


		// �ϥ�winsock�D����Ҧ��A�ҥH���|�d�b�Y�禡��(�p�Gfgets, recv)�A�j��|���_������M�ˬd���A�D
		while (1)
		{
			keyboard_state = ::_kbhit(); // �ˬd�O�_����L���U�A�Y���Ǧ^1�A�Y�L�Ǧ^0
			if (keyboard_state > 0)
			{
				ch = ::_getch(); // ���o�q��L���U���@�����
				printf("%c", ch);
				if (ch == ENTER_KEY_CODE) // �Y���UEnter��...
				{
					printf("\n");
					if (::strcmp(buff_message, "exit") == 0) // ����r��I�Y�� exit�A�h�������{��
						break;

					if (buff_message_length > 0)
					{
						Send_C2S_Message(buff_message, buff_message_length);

						buff_message_length = 0;
						::memset(buff_message, 0, PACKET_BUFFER_MAX); // ��buff_message�O����M����0

						Client.pAliveNode->StartTime = 0; // �߸��]���s�p��
					}
				}
				else if (ch == BACKSPCAE_KEY_CODE) // �Y���U�˰h��...
				{
					if (buff_message_length - 1 >= 0)
						buff_message[--buff_message_length] = 0;
				}
				else // �s�J��ѰT��...
				{
					if (buff_message_length + 1 < PACKET_BUFFER_MAX)
						buff_message[buff_message_length++] = ch;
				}
			}

			if (RecvEx(Client.Socket) == false) // recv�]�D����^�G�|���_���ձ���server�ݶǨӪ��ʥ]
				break;

			TaskManager->vRunTask();

			::Sleep(1); // ����1�@��
		}
	}
	
	void Send_C2S_Alive()
	{
		C2S_Alive packet = {};

		packet.PacketLength = sizeof(HeadPacketInfo);
		packet.PacketType = PACKET__C2S_ALIVE;
		packet.PacketOrderNumber = ++Client.C2S_PacketOrderNumber;

		SendEx(Client.Socket, (char*)&packet, packet.PacketLength); // send �ǰe�ʥ]
	}
	
	void Send_C2S_Name(char* buff_name, UINT name_length)
	{
		C2S_Name packet = {};

		packet.PacketLength = sizeof(HeadPacketInfo) + name_length;
		packet.PacketType = PACKET__C2S_NAME;
		packet.PacketOrderNumber = ++Client.C2S_PacketOrderNumber;
		::strcpy_s(packet.Name, NAME_BUFFER_MAX, buff_name);

		SendEx(Client.Socket, (char*)&packet, packet.PacketLength); // send �ǰe�ʥ]
	}

	void Send_C2S_Message(char* buff_message, UINT buff_message_length)
	{
		C2S_Message packet = {};

		packet.PacketLength = sizeof(HeadPacketInfo) + buff_message_length;
		packet.PacketType = PACKET__C2S_MESSAGE;
		packet.PacketOrderNumber = ++Client.C2S_PacketOrderNumber;
		::strcpy_s(packet.Message, MESSAGE_BUFFER_MAX, buff_message);

		SendEx(Client.Socket, (char*)&packet, packet.PacketLength); // send �ǰe�ʥ]
	}

	bool SendEx(SOCKET socket, char* packet, WORD packet_length)
	{
		int  send_packet_max = packet_length;
		int  send_length = 0;
		int  buff_packet_index = 0;

		int  send_retry_count = 0;
		const int send_retry_max = 5000;

		while (send_packet_max > 0)
		{
			// �ǰe�ʥ]
			send_length = ::send(socket, &packet[buff_packet_index], send_packet_max, 0);
			if (send_length > 0)
			{
				send_packet_max -= send_length;
				buff_packet_index += send_length;
			}
			else
			{
				int error = ::WSAGetLastError();
				if (error == WSAEWOULDBLOCK) // 10035
				{
					if (++send_retry_count < send_retry_max)
					{
						::Sleep(1);
						continue;
					}
					printf("send failed, WSAEWOULDBLOCK, send_retry_count full, socket= %d, error: %d\n", socket, error);
					return false; // ��socket�i��w�g�o�Ϳ��~.
				}
				printf("send failed, socket= %d, error: %d\n", socket, error);
				return false;
			}
		}
		printf("send okay, send_length: %d, socket= %d\n", send_length, socket);
		return false;
	}

	bool RecvEx(SOCKET socket)
	{
		int recv_length = 0;
		char* buffer = (char*)&RecvPacket;

		const int recv_retry_max = 5000;
		int recv_retry_count = 0;

		while (RecvPacketLength > 0)
		{
			// �����ʥ]�G���o�ʥ]���Y-�`����
			recv_length = ::recv(socket, &buffer[RecvPacketBufferIndex], RecvPacketLength, 0);
			if (recv_length > 0)
			{
				RecvPacketLength -= recv_length;
				RecvPacketBufferIndex += recv_length;
			}
			else if (recv_length == 0)
			{
				printf("#1 recv notice, client connection closed, socket= %d\n", socket);
				return false;
			}
			else
			{
				int error = ::WSAGetLastError();
				if (error == WSAEWOULDBLOCK) // 10035 - recv�w�İϤ��S���ʥ]�F...
				{
					return true; // ��client�O�D����Ҧ��A�ҥH���ձ������Y�o�S��ƮɡA�N�ߧY��^�D
				}
				printf("#1 recv failed, socket= %d, error: %d\n", socket, error);
				return false;
			}
		}

		RecvPacketLength = RecvPacket.PacketLength - PACKET_LENGTH_SIZE;

		while (RecvPacketLength > 0)
		{
			// �����ʥ]�G���o�@�ӧ��㪺�ʥ]����
			recv_length = ::recv(socket, &buffer[RecvPacketBufferIndex], RecvPacketLength, 0);
			if (recv_length > 0)
			{
				RecvPacketLength -= recv_length;
				RecvPacketBufferIndex += recv_length;
			}
			else if (recv_length == 0)
			{
				printf("#2 recv notice, client connection closed, socket= %d\n", socket);
				ClearRecvPacketInfo();
				return false;
			}
			else
			{
				int error = ::WSAGetLastError();
				if (error == WSAEWOULDBLOCK) // 10035 - recv�w�İϤ��S���ʥ]�F...
				{
					if (++recv_retry_count < recv_retry_max)
					{
						::Sleep(1);
						continue;
					}
					printf("#2 recv failed, WSAEWOULDBLOCK, recv_retry_count full, socket= %d, error: %d\n", socket, error);
					ClearRecvPacketInfo();
					return false; // ��socket�i��L�k�A����ʥ]�F�A��@�w�g�_�u.
				}
				printf("#2 recv failed, socket= %d, error: %d\n", socket, error);
				ClearRecvPacketInfo();
				return false;
			}
		}

		if (RecvPacket.PacketOrderNumber == ++Client.S2C_PacketOrderNumber)
			printf("recv okay, socket= %d, packet_length= %d, [%d] buffer= %s\n", socket, RecvPacketBufferIndex, RecvPacket.PacketOrderNumber, RecvPacket.Buffer);
		else
			printf("recv okay, ERROR PacketOrderNumber : socket= %d, packet_length= %d, server[%d], client[%d], buffer= %s\n", socket, RecvPacketBufferIndex, RecvPacket.PacketOrderNumber, Client.S2C_PacketOrderNumber, RecvPacket.Buffer);

		ClearRecvPacketInfo();

		return true;
	}
	void ClearRecvPacketInfo()
	{
		RecvPacket = {};
		RecvPacketLength = PACKET_LENGTH_SIZE;
		RecvPacketBufferIndex = 0;
	}

	void Shutdown()
	{
		// shutdown�u���������@�ӳs���I�|���ݤ@�p�q�ɶ��A����S����h�ʥ]�n�ǰe�ɡA�~���_�s���I
		// SD_SEND�G���wsocket ���U�Ӥ���A�I�s�ǰe�ʥ]���禡�C
		int result = ::shutdown(Client.Socket, SD_SEND);
		if (result == SOCKET_ERROR)
		{
			printf("shutdown failed, error: %d\n", WSAGetLastError());
			return;
		}

		::Sleep(100); // ����100�@��I���Qshutdown��client������ɶ��B�z���u�ʧ@�I

		char buff_message[PACKET_BUFFER_MAX] = { 0 };

		// �{�������e���ʧ@�G�Y���T������Arecv�|�^��0�A�Y�i�u���������{���D
		// �Y�^��-1�A�åB���~�X�OWSAEWOULDBLOCK�ɡA���O�u���������{���D
		do
		{
			// recv�]�D����^�G�|���ձ���server�ݶǨӪ��ݦs�ʥ]�H�����u�T�{�ʥ]...(�Y������)
			result = ::recv(Client.Socket, buff_message, PACKET_BUFFER_MAX, 0);
			if (result > 0)
				printf("final recv okay, recv_length: %d\n", result);
			else if (result == 0)
				printf("final recv notice, client connection closed.\n");
			else
			{
				int error = ::WSAGetLastError();
				if (error == WSAEWOULDBLOCK) // 10035 �S������s�ʥ]��...
				{
					printf("final recv notice, client connection closed. - WSAEWOULDBLOCK\n");
				}
				else
				{
					printf("final recv failed, error: %d\n", error);
					break;
				}
			}

		} while (result > 0);
	}

	virtual void vOnRunTask(TaskNode* node)
	{
		switch (node->TaskNumber)
		{
		case TASK__ALIVE:
		{
			Send_C2S_Alive();
			printf("vOnRunTask   node->TaskNumber= %d, send alive packet.\n", node->TaskNumber);
			break;
		}
		}
	}

	void Release()
	{
		Shutdown();

		if (Client.Socket != INVALID_SOCKET)
			::closesocket(Client.Socket); // ����socket
		Client.Socket = INVALID_SOCKET;

		WSACleanup(); // ���� winsock

		MY_RELEASE(TaskManager);
	}

	void Run()
	{
		Logger::Create();

		const UINT one_KB = 1024;
		const UINT one_MB = (one_KB * 1024);
		const UINT memory_pool_bytes_max = one_MB * 100;
		MainMemoryPool::sMemoryPoolCreate(memory_pool_bytes_max);

		if (Create())
		{
			NameInput();
			ChatInput();
		}
		Release();

		MainMemoryPool::sMemoryPoolRelease();
		MemoryCounter::sShow_MemoryUseCount();
	}
};

int main()
{
	TCPClient client;
	client.Run();

	getc(stdin);
	return 0;
}