/* source: xio-fuse.c */
/* Copyright Abraham Sue */
/* Published under the GNU General Public License V.2, see file COPYING */

/* this file contains the source for opening addresses of fuse type */

#include "xiosysincludes.h"
#if WITH_FUSE
#include "xioopen.h"

#include "xio-fuse.h"


static int xioopen_fuse(int argc, const char *argv[], struct opt *opts, int xioflags, xiofile_t *fd, unsigned groups, int dummy1, int dummy2, int dummy3);

/****** FUSE addresses ******/
const struct optdesc opt_fuse_mountpoint = { "mp",           NULL,      OPT_FUSE_MP,          GROUP_FD,          PH_OPEN, TYPE_FILENAME, OFUNC_SPEC };
const struct optdesc opt_fuse_option     = { "option",       NULL,      OPT_FUSE_NAME,        GROUP_FD, PH_FD,   TYPE_STRING,   OFUNC_SPEC };
const struct optdesc opt_fuse_fstype     = { "fstype",       NULL,      OPT_FUSE_FSTYPE,      GROUP_FD, PH_FD,   TYPE_STRING,   OFUNC_SPEC };
const struct optdesc opt_fuse_lowlevel   = { "lowlevel",     NULL,      OPT_FUSE_LOWLEVEL,      GROUP_FD, PH_FD,   TYPE_STRING,   OFUNC_SPEC };
#if LATER
const struct optdesc opt_route           = { "route",           NULL,          OPT_ROUTE,           GROUP_INTERFACE, PH_INIT, TYPE_STRING,   OFUNC_SPEC };
#endif

static const struct xioaddr_endpoint_desc xioendpoint_fuse1    = { XIOADDR_SYS, "fuse",   1, XIOBIT_ALL, GROUP_FD|GROUP_NAMED|GROUP_OPEN, XIOSHUT_CLOSE, XIOCLOSE_NONE, xioopen_fuse, 0, 0, 0 HELP(":mount-point") };

const union xioaddr_desc *xioaddrs_fuse[] = {
   (union xioaddr_desc *)&xioendpoint_fuse1,
   NULL
};

#if LATER
/* sub options for route option */
#define IFOPT_ROUTE 1
static const struct optdesc opt_route_tos = { "route", NULL, IFOPT_ROUTE, };
static const struct optname xio_route_options[] = {
   {"tos", &xio_route_tos }
} ;
#endif

static int xioopen_fuse(int argc, const char *argv[], struct opt *opts, int xioflags, xiofile_t *xfd, unsigned groups, int dummy1, int dummy2, int dummy3) {
   char *fuse_mp= NULL;
   char *fuse_device="/dev/fuse";
   char *fuse_option = NULL, *fuse_fstype = NULL;
   int pf = /*! PF_UNSPEC*/ PF_INET;
   struct xiorange network;
   bool no_pi = false;
   const char *namedargv[] = { "fuse", NULL, NULL };
   int rw = (xioflags & XIO_ACCMODE);
   bool exists;
   struct ifreq ifr;
   int sockfd;
   char *ifaddr;
   int result;

   if (argc != 2) {
      Error2("%s: wrong number of parameters (%d instead of 1)",
	     argv[0], argc-1);
   }

   if (retropt_string(opts, OPT_FUSE_MP, &fuse_mp) != 0) {
      Error2("Mount point %s is not available\n", fuse_mp);
   }

   /*! socket option here? */
   retropt_socket_pf(opts, &pf);

   namedargv[1] = fusedevice;
   /* open the fuse cloning device */
   if ((result = _xioopen_named_early(2, namedargv, xfd, groups, &exists, opts)) < 0) {
      return result;
   }

   /*========================= the fusenel interface =========================*/
   Notice("creating fusenel network interface");
   if ((result = _xioopen_open(fusedevice, rw, opts)) < 0)
      return result;
   if (XIOWITHRD(rw))  xfd->stream.rfd = result;
   if (XIOWITHWR(rw))  xfd->stream.wfd = result;

   /* prepare configuration of the new network interface */
   memset(&ifr, 0,sizeof(ifr));

   if (retropt_string(opts, OPT_FUSE_NAME, &fusename) == 0) {
      strncpy(ifr.ifr_name, fusename, IFNAMSIZ);	/* ok */
      free(fusename);
   } else {
      ifr.ifr_name[0] = '\0';
   }

   ifr.ifr_flags = IFF_FUSE;
   if (retropt_string(opts, OPT_FUSE_TYPE, &fusetype) == 0) {
      if (!strcmp(fusetype, "tap")) {
	 ifr.ifr_flags = IFF_TAP;
      } else if (strcmp(fusetype, "fuse")) {
	 Error1("unknown fuse-type \"%s\"", fusetype);
      }
   }

   if (retropt_bool(opts, OPT_IFF_NO_PI, &no_pi) == 0) {
      if (no_pi) {
	 ifr.ifr_flags |= IFF_NO_PI;
#if 0 /* not neccessary for now */
      } else {
	 ifr.ifr_flags &= ~IFF_NO_PI;
#endif
      }
   }

   if (Ioctl(xfd->stream.rfd, FUSESETIFF, &ifr) < 0) {
      Error3("ioctl(%d, FUSESETIFF, {\"%s\"}: %s",
	     xfd->stream.rfd, ifr.ifr_name, strerror(errno));
      Close(xfd->stream.rfd);
   }

   /*===================== setting interface properties =====================*/

   /* we seem to need a socket for manipulating the interface */
   if ((sockfd = Socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
      Error1("socket(PF_INET, SOCK_DGRAM, 0): %s", strerror(errno));
      sockfd = xfd->stream.rfd;	/* desparate fallback attempt */
   }

   /*--------------------- setting interface address and netmask ------------*/
   if (argc == 2) {
      if ((ifaddr = strdup(argv[1])) == NULL) {
	 Error1("strdup(\"%s\"): out of memory", argv[1]);
	 return STAT_RETRYLATER;
      }
      if ((result = xioparsenetwork(ifaddr, pf, &network)) != STAT_OK) {
	 /*! recover */
	 return result;
      }
      socket_init(pf, (union sockaddr_union *)&ifr.ifr_addr);
      ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr =
	 network.netaddr.ip4.sin_addr;
      if (Ioctl(sockfd, SIOCSIFADDR, &ifr) < 0) {
	 Error4("ioctl(%d, SIOCSIFADDR, {\"%s\", \"%s\"}: %s",
		sockfd, ifr.ifr_name, ifaddr, strerror(errno));
      }
      ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr =
	 network.netmask.ip4.sin_addr;
      if (Ioctl(sockfd, SIOCSIFNETMASK, &ifr) < 0) {
	 Error4("ioctl(%d, SIOCSIFNETMASK, {\"0x%08u\", \"%s\"}, %s",
		sockfd, ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr,
		ifaddr, strerror(errno));
      }
      free(ifaddr);
   }
   /*--------------------- setting interface flags --------------------------*/
   applyopts_single(&xfd->stream, opts, PH_FD);

   if (Ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
      Error3("ioctl(%d, SIOCGIFFLAGS, {\"%s\"}: %s",
	     sockfd, ifr.ifr_name, strerror(errno));
   }
   Debug2("\"%s\": system set flags: 0x%hx", ifr.ifr_name, ifr.ifr_flags);
   ifr.ifr_flags |= xfd->stream.para.fuse.iff_opts[0];
   ifr.ifr_flags &= ~xfd->stream.para.fuse.iff_opts[1];
   Debug2("\"%s\": xio merged flags: 0x%hx", ifr.ifr_name, ifr.ifr_flags);
   if (Ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
      Error4("ioctl(%d, SIOCSIFFLAGS, {\"%s\", %hd}: %s",
	     sockfd, ifr.ifr_name, ifr.ifr_flags, strerror(errno));
   }
   ifr.ifr_flags = 0;
   if (Ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
      Error3("ioctl(%d, SIOCGIFFLAGS, {\"%s\"}: %s",
	     sockfd, ifr.ifr_name, strerror(errno));
   }
   Debug2("\"%s\": resulting flags: 0x%hx", ifr.ifr_name, ifr.ifr_flags);


#if LATER
   applyopts_named(fusedevice, opts, PH_FD);
#endif
   applyopts(xfd->stream.rfd, opts, PH_FD);
   applyopts_cloexec(xfd->stream.rfd, opts);

   applyopts_fchown(xfd->stream.rfd, opts);

   if ((result = _xio_openlate(&xfd->stream, opts)) < 0)
      return result;

   return 0;
}

#endif /* WITH_FUSE */
