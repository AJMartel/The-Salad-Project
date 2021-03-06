/*
BSD 3-Clause License

Copyright (c) 2017, John Ventura
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include "interface.h"
#include "rand.h"
#include "getcreds.h"
#include "nameres.h"
#include "destroy.h"

#define MAXPACKETLEN 0xFFFF

char *fakehost;
int seconds;
int timemode;
int verbose;

int getrawsock(char *intname) {
    struct ifreq ifr;
    int sock;

    if ((sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP))) < 0) {
	perror("can't open raw socket");
	exit(1);
    }

    memset(&ifr, 0x00, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, intname, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
	perror("can't get socket info");
	exit(1);
    }
    ifr.ifr_flags |= IFF_PROMISC;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
	perror("can't set promic");
	exit(1);
    }
    if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
	perror("can't retrieve etherenet index\n");
	exit(1);
    }
    return (sock);
}

void *sendqueries() {
    int waittime;
    waittime = seconds;
    for (;;) {
	fillstr(fakehost, NBNAMEMIN + getrand8() % (NBNAMELEN - NBNAMEMIN),
		CHARALPHAUPPER);
	sleep(1);
	queryhost(fakehost);
	if (timemode == 1)
	    waittime = getrand8() % (seconds * 2);
	if (verbose) {
	    printf("sleeping for %i seconds\n", waittime);
	}
	sleep(waittime);
    }
    pthread_exit(NULL);
}

void usage(char *progname) {
    printf
	("%s detects and disrupts broadcast name resolution MiTM attacks\n",
	 progname);
    printf("\t-i\tinterface to monitor (e.g. \"eth0\")\n");
    printf("\t-u\ttab delimited list of usernames and passwords\n");
    printf("\t-t\ttime in seconds between probes\n");
    printf("\t-p\taverage time in seconds between probes\n");
    printf("\t-d\tresponses to MiTM detectoin\n");
    printf("\t\tlog\tlogs the detection\n");
    printf("\t\thash\tsends credentials from the file -u file\n");
    printf("\t\tflood\tperform a resource exhaustion attack\n");
    printf("\t-s\tspecify a known spoofing server instead of detection\n");
    printf("\t-v\tverbose mode\n");
    printf("\n\nExamples:\n");
    printf("\t%s -d \"log hash\" -u ./usernames.txt -t 10 -i eth0\n",
	   progname);
    printf
	("\tThis looks for MiTM servers ever 10 seconds. It also sends a fake hash from the \"usernames.txt\" file and logs the detection\n");
    printf("\t%s -d \"flood\" -s 10.0.0.23\n", progname);
    printf("\tThis floods the MiTM server at 10.0.0.23.\n");
}

int main(int argc, char *argv[]) {
    int sock;
    int pktlen;
    int c;
    uint8_t *pkt;
    char *resolved;
    struct iphdr *ip;
    struct udphdr *udp;
    pthread_t threads[5];
    char *interface = NULL;
    char **user = NULL;
    char **passwd = NULL;
    struct creddb *cdb = NULL;
    char *server = NULL;
    uint32_t serverip;
    int destroymask = DES_LOG;


    verbose = 0;
    seconds = 300;		// default time interval
    timemode = 0;		// 0 == deterministic time
    user = NULL;

    while ((c = getopt(argc, argv, "hi:u:t:p:d:s:v")) != -1) {
	switch (c) {
	case 'h':		// probably help
	    usage(argv[0]);
	    return (0);
	    break;
	case 'i':		// interface to monitor
	    interface = optarg;
	    break;
	case 'u':		// list of users for hashes
	    cdb = getuserlist(optarg);
	    user = cdb->users;
	    passwd = cdb->passwords;
	    break;
	case 't':		// deterministic time
	    seconds = atoi(optarg);
	    break;
	case 'p':		// probabilistic time
	    seconds = atoi(optarg);
	    timemode = 1;
	    break;
	case 'd':		// options for "destroy" log|flood|hash
	    destroymask = getdestroymask(optarg);
	    break;
	case 's':		// server to "destroy"
	    server = optarg;
	    break;
	case 'v':
	    verbose++;
	    break;
	default:
	    break;
	}
    }

    if (server != NULL) {
	serverip = resolvehost4(server);
	resolved = "none";
	destroy(serverip, resolved, user, passwd, destroymask);
	return (0);
    }
    if (interface == NULL) {
	interface = (char *) malloc(MAXINTLEN);
	if (interface == NULL) {
	    perror("can't allocaet memory");
	    exit(1);
	}
	guessintname(interface, MAXINTLEN);
    }
    remoteip = getipv4addr(interface);
    remotebcast = getipv4bcast(interface);

    fakehost = (char *) malloc(NBNAMELEN + 1);
    if (fakehost == NULL) {
	perror("can't allocate memory");
	exit(1);
    }
    memset(fakehost, 0x00, NBNAMELEN + 1);

    if (pthread_create(&threads[0], NULL, sendqueries, NULL) != 0) {
	perror("can't thread");
	exit(0);
    }

    resolved = (char *) malloc(NBNAMELEN);
    if (resolved == NULL) {
	perror("can't allocate memory");
	exit(1);
    }

    pkt = (uint8_t *) malloc(MAXPACKETLEN);
    if (pkt == NULL) {
	perror("can't allocate memory");
	exit(1);
    }
    memset(pkt, 0x00, MAXPACKETLEN);
    ip = (struct iphdr *) (pkt + sizeof(struct ethhdr));
    udp =
	(struct udphdr *) (pkt + sizeof(struct iphdr) +
			   sizeof(struct ethhdr));

    printf("Listening on interface %s\n", interface);
    sock = getrawsock(interface);

    for (pktlen = 0; pktlen >= 0; pktlen = read(sock, pkt, MAXPACKETLEN)) {
	if (ip->protocol == 0x11) {
	    if (udp->dest == (htons(137))) {
		memset(resolved, 0x00, NBNAMELEN);
		parsenbns((uint8_t *) ip,
			  MAXPACKETLEN - (sizeof(struct iphdr) +
					  sizeof(struct iphdr)), resolved);
	    } else if (udp->source == (htons(5355))) {

		parsellmnr((uint8_t *) ip,
			   MAXPACKETLEN - (sizeof(struct iphdr) +
					   sizeof(struct iphdr)),
			   resolved);
	    }
	    if ((strlen(resolved) > 0)
		&& (!strncasecmp(resolved, fakehost, strlen(fakehost)))) {
		destroy(ip->saddr, resolved, user, passwd, destroymask);
		resolved[0] = 0x00;	//no more destroying
	    }
	}
	memset(pkt, 0x00, MAXPACKETLEN);
    }

    return (0);
}
