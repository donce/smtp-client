#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <string>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <termios.h>

#include <netdb.h>

#define PORT 25
#define BUFFER_SIZE 1024*1024
#define DEBUG 0

using namespace std;

int s;


static char base64_table[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/'
};

static char *decoding_table = NULL;
static int mod_table[] = {0, 2, 1};


string base64(string raw) {
    int output_length = 4 * ((raw.length() + 2) / 3);

    char *encoded_raw = (char*)malloc(output_length+1);
    if (encoded_raw == NULL) return "";
	encoded_raw[output_length] = 0;

    for (int i = 0, j = 0; i < raw.length();) {
        uint32_t octet_a = i < raw.length() ? raw[i++] : 0;
        uint32_t octet_b = i < raw.length() ? raw[i++] : 0;
        uint32_t octet_c = i < raw.length() ? raw[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_raw[j++] = base64_table[(triple >> 3 * 6) & 0x3F];
        encoded_raw[j++] = base64_table[(triple >> 2 * 6) & 0x3F];
        encoded_raw[j++] = base64_table[(triple >> 1 * 6) & 0x3F];
        encoded_raw[j++] = base64_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[raw.length() % 3]; i++)
        encoded_raw[output_length - 1 - i] = '=';

	string s(encoded_raw);
	free(encoded_raw);
    return s;
}

char buffer[BUFFER_SIZE];

string recvString() {
	int r = recv(s, buffer, BUFFER_SIZE, 0);
	string ans = buffer;
	if (DEBUG)
		cout << "S: " << ans;
	return ans;
}

struct Response {
	int status;
	string message;
	bool error() {
		return status / 100 == 5 || status / 100 == 4;
	}
};

Response recvResponse() {
	string msg = recvString();
	int space;
	for (space = 0; space < msg.length() && msg[space] != ' '; ++space);
	Response r;
	r.status = atoi(msg.substr(0, space).c_str());
	r.message = msg.substr(space+1, msg.length()-1); 
	return r;
}

Response sendString(string str) {
	str += '\n';
	if (DEBUG)
		cout << "C: " << str;
	send(s, str.c_str(), str.length(), 0);
	return recvResponse();
}

string getPassword() {
	const char BACKSPACE=127;
	const char RETURN=10;

	string password;
	unsigned char ch=0;

	struct termios t_old, t_new;

	tcgetattr(STDIN_FILENO, &t_old);
	t_new = t_old;
	t_new.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &t_new);
	while ((ch = getchar()) != RETURN) {
		if (ch == BACKSPACE) {
			if (password.length()!=0) {
				cout <<"\b \b";
				password.resize(password.length()-1);
			}
		}
		else {
			password += ch;
			cout << '*';
		}
	}
	tcsetattr(STDIN_FILENO, TCSANOW, &t_old);
	cout << endl;
	return password;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		cout << "Usage: " << argv[0] << " server" << endl;
		return 1;
	}

	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	struct addrinfo *result;
	if (getaddrinfo(argv[1], "25", &hints, &result) != 0) {
		cout << "Failed getting address info!" << endl;
		return 1;
	}

	s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (s == -1) {
		cout << "Failed creating socket!" << endl;
		return 1;
	}
	if (connect(s, result->ai_addr, result->ai_addrlen) == -1) {
		cout << "Error connecting!" << endl;
		return 1;
	}
	freeaddrinfo(result);
	
	if (DEBUG)
		cout << "Connected" << endl;

	recvResponse();
	sendString(string("HELO ") + argv[1]);

	cout << "---Authentication---" << endl;
	string username, password;
	cout << "Username: ";
	cin >> username;
	cin.ignore();
	cout << "Password: ";
	password = getPassword();
	
	sendString("AUTH LOGIN");
	sendString(base64(username));
	if (sendString(base64(password)).error()) {
		cout << "Authentication failed!" << endl;
		return 1;
	}
	
	bool writeEmail = true;
	while (writeEmail) {
		cout << "-----New email------" << endl;
		string from, to, subject, message;

		cout << "From: ";
		cin >> from;
		if (sendString("MAIL FROM:<" + from + ">").error()) {
			cout << "Wrong sender!" << endl;
			return 1;
		}

		cout << "To: ";
		cin >> to;
		if (sendString("RCPT TO:<" + to + ">").error()) {
			cout << "Wrong recipient!" << endl;
			return 1;
		}

		cout << "Subject: ";
		cin >> subject;
		cin.ignore();

		cout << "Message (end with empty line):" << endl;
		string line;
		getline(cin, line);
		while (!line.empty()) {
			message += line + '\n';
			getline(cin, line);
		}

		cout << "Sending email..." << endl;

		sendString("DATA");
		string fromLine, toLine, subjectLine;
		fromLine = "From: <" + from + ">\n";
		toLine = "To: <" + to + ">\n";
		subjectLine = "Subject: " + subject + "\n";
		if (sendString(fromLine + toLine + subjectLine + "\n" + message + "\n.").error()) {
			cout << "Failed sending email!" << endl;
			return 1;
		}

		
		cout << "Email sent." << endl;

		cout << "Write new email(y)?";
		char c = getchar();
		writeEmail = (c == 'Y' || c == 'y');
	}

	sendString("QUIT");
}
