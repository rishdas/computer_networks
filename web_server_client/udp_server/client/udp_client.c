#include<stdio.h>
#include<stdlib.h>
#include<netdb.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<unistd.h>
#include<string.h>
#include<signal.h>
#include<sys/time.h>


FILE *debg_ofp;
FILE     *ifp;
int  client_sock_fd;
#define MAX_BUFFER 1000
typedef struct client_info_
{
    int            port;
    char           *file_name;
    struct hostent *server_addr;
    int            is_filename_in_disk;
} client_info_t;
    
int cleanup(int serv_sock_fd, FILE *debg_ofp)
{
    if (serv_sock_fd >= 0) {
	close(serv_sock_fd);
    }
    if (debg_ofp != NULL) {
	fclose(debg_ofp);
    }
    return 0;
}

int parse_cmd_line_args(int argc, char *argv[],
			client_info_t *client_info, FILE *debg_ofp)
{   
    if(argc< 4) {
	fprintf(debg_ofp, "ERROR: Very few arguments "\
		"the format for is \n"\
		"client\t<server_host>\t<server_port>" \
		"\t<connection_type>\t<filename.txt>\n");
	return -1;
	
    }
    bzero(client_info, sizeof(client_info));
    client_info->server_addr     = gethostbyname(argv[1]);
    client_info->port            = atoi(argv[2]);
    client_info->file_name       = argv[3];
    client_info->is_filename_in_disk = 0;
    ifp = fopen(client_info->file_name, "r");

    if (ifp == NULL) {
	fprintf(debg_ofp,
		"File not found in disk hence requesting from server\n");
	client_info->is_filename_in_disk = 1;
    }

    
    fprintf(debg_ofp, "Parsed command line arguments\n");
    fprintf(debg_ofp, "Server Address: %s\t",
	    client_info->server_addr->h_name);
    fprintf(debg_ofp, "Port No: %d\t", client_info->port);
    fprintf(debg_ofp, "File Name: %s\t\n", client_info->file_name);
    return 0;
    
}

int bootstrap_client(int *client_sock_fd, client_info_t client_info,
		     struct sockaddr_in *server_addr, FILE *debg_ofp)
{
    int                status;

    *client_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*client_sock_fd < 0) {
	fprintf(debg_ofp, "Error opening socket\n");
	fprintf(stderr, "Error opening socket\n");
	return 1;
    }
    bzero((char *) server_addr, sizeof(server_addr));
    server_addr->sin_family = AF_INET;
    bcopy((char *)client_info.server_addr->h_addr,
	  (char *)&server_addr->sin_addr.s_addr,
	  client_info.server_addr->h_length);
    server_addr->sin_port = htons(client_info.port);
    return 0;
}

int build_http_get_request(client_info_t req_info,
			   char *resp_buf, FILE *debg_ofp)
{
    int           http_version;
    char          file_name[MAX_BUFFER];
    int           status;
    static int    get_once = 0;
    if (req_info.is_filename_in_disk == 0) {
	status = fscanf(ifp, "%s\n", file_name);
	if (status <= 0) {
	    return -1;
	}
    } else {
	if (get_once == 1) {
	    return -1;
	}
	strncpy(file_name, req_info.file_name,
		strlen(req_info.file_name));
	get_once = 1;
    }
    
    sprintf(resp_buf, "GET /%s %s\r\n", file_name, "HTTP/1.0");
    sprintf(resp_buf, "%sHost: %s:%d\r\n", resp_buf,
	    req_info.server_addr->h_name, req_info.port);
    sprintf(resp_buf, "%sConnection: %s\r\n", resp_buf, "close");
    sprintf(resp_buf, "%sUser-Agent: %s\r\n", resp_buf, "Lannister");
    sprintf(resp_buf, "%sAccept: %s\r\n", resp_buf, "text/html");
    sprintf(resp_buf, "%sAccept-Language: %s\r\n", resp_buf, "en-US,en;q=0.5");
    sprintf(resp_buf, "%sAccept-Encoding: %s\r\n", resp_buf, "gzip, deflate");
    
    fprintf(debg_ofp, "INFO: GET Response: \n %s\n", resp_buf);
    return 0;
}
int get_seg_no_max_seg_no(char *buf, int *seg_no,
			  int *max_seg_no, FILE *debg_ofp)
{
    char *pos;
    char segment_no[5];
    char max_segment_no[5];
    pos = strstr(buf, "Segment: ");
    if (pos == NULL) {
	return 1;
    }
    pos = pos + strlen("Segment: ");
    sscanf(pos, "%s %s\r\n", segment_no, max_segment_no);
    fprintf(stdout, "\nSegment no: %s  Max Segment No: %s\n",
	    segment_no, max_segment_no);
    *seg_no = atoi(segment_no);
    *max_seg_no = atoi(max_segment_no);
    fprintf(stdout, "\nSegment no: %d  Max Segment No: %d\n",
	    *seg_no, *max_seg_no);
    return 0;
}
    
int check_and_recieve_further_segments(int client_sock_fd,
				       char *buf, FILE *debg_ofp)
{
    int seg_no;
    int max_seg_no;
    int status;
    status =
	get_seg_no_max_seg_no(buf , &seg_no, &max_seg_no, debg_ofp);
    if (status == 1) {
	return status;
    }
    bzero(buf, MAX_BUFFER);
    while (seg_no < max_seg_no)
    {
	status = recvfrom(client_sock_fd, buf, MAX_BUFFER, 0, NULL, NULL);
	if (status < 0) {
	    fprintf(stderr, "Error reading from the socket\n");
	    fprintf(debg_ofp, "Error reading into the socket\n");
	    close(client_sock_fd);
	    return -1;
	}
	fprintf(stdout, "Incoming Message: \n %s", buf);
	fprintf(debg_ofp, "Incoming Message: \n %s", buf);
	status =
	    get_seg_no_max_seg_no(buf, &seg_no, &max_seg_no, debg_ofp);
	bzero(buf, MAX_BUFFER);
    }
    
    return 0;
}
int get_from_server(int client_sock_fd, client_info_t client_info,
		    struct sockaddr_in server_addr, FILE *debg_ofp)
{
    char               buf[MAX_BUFFER];
    int                status;
    int                first_time = 0;
    long               elapsed;
    struct timeval     recv_time, reply_time;
    
    while(1) {
	if(first_time != 0) {
	    status = bootstrap_client(&client_sock_fd, client_info,
				      &server_addr, debg_ofp);
	    if(status != 0) {
		fprintf(stderr, "ERROR: Creating a socket\n");
		fprintf(debg_ofp, "ERROR: Creating a socket\n");
		fclose(debg_ofp);
		exit(1);
	    }
	}
	status = build_http_get_request(client_info, buf, debg_ofp);
	if (status == -1) {
	    fprintf(debg_ofp, "Error building request silently exiting\n");
	    close(client_sock_fd);
	    return 0;
	}
	status = sendto(client_sock_fd, buf, strlen(buf), 0,
			(struct sockaddr *)&server_addr, sizeof(server_addr));
	if (status < 0) {
	    fprintf(stderr, "Error writing into the socket\n");
	    fprintf(debg_ofp, "Error writing into the socket\n");
	    close(client_sock_fd);
	    return -1;
	}
	gettimeofday(&recv_time, 0);
	bzero(buf, MAX_BUFFER);
	status = recvfrom(client_sock_fd, buf, MAX_BUFFER, 0, NULL, NULL);
	if (status < 0) {
	    fprintf(stderr, "Error reading from the socket\n");
	    fprintf(debg_ofp, "Error reading into the socket\n");
	    close(client_sock_fd);
	    return -1;
	}
	fprintf(stdout, "Incoming Message: \n %s", buf);
	fprintf(debg_ofp, "Incoming Message: \n %s", buf);
	status = check_and_recieve_further_segments(client_sock_fd,
						    buf, debg_ofp);
	if (status == 1) {
	    continue;
	}
	if (status == -1) {
	    return status;
	}
	gettimeofday(&reply_time, 0);
	elapsed = (reply_time.tv_sec-recv_time.tv_sec)*1000000
	    + reply_time.tv_usec-recv_time.tv_usec;
	fprintf(stdout, "\nTime Elapsed: %ld\n", elapsed);
	bzero(buf, MAX_BUFFER);
	close(client_sock_fd);
	first_time = 1;
    }
    return 0;
}
int main(int argc, char *argv[])
{

    int                status;
    client_info_t      client_info;
    char               file_name[] = "udp_client_trace.log";
    struct sockaddr_in server_addr;
    
    signal(SIGPIPE, SIG_IGN);
    debg_ofp = fopen(file_name, "w");
    status = parse_cmd_line_args(argc, argv, &client_info, debg_ofp);
    if(status != 0) {
	fprintf(stderr, "ERROR: Invalid arguments check log\n");
	fclose(debg_ofp);
	exit(1);
    }
    status = bootstrap_client(&client_sock_fd, client_info,
			      &server_addr, debg_ofp);
    if(status != 0) {
	fprintf(stderr, "ERROR: Creating a socket\n");
	fprintf(debg_ofp, "ERROR: Creating a socket\n");
	fclose(debg_ofp);
	exit(1);
    }
    
    get_from_server(client_sock_fd, client_info,
		    server_addr,debg_ofp);
    fclose(debg_ofp);
    return 0;
}



