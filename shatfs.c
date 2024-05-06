/*
 *	shatfs.c   v0.7
 *	by adventuresin9
 *	
 *	A filesystem for the 
 *	Raspberry Pi
 *	Sense Hat
 *	
 *	LSP25H		0x5C	temp/press
 *	HTS221		0x5F	temp/humid
 *	LSM9DS1		0x1C	magneto
 *	LSM9DSI		0x6A	gyro/accel
 *	LED2472G	0x46	LED grid
 */



#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>


typedef struct Devfile Devfile;
typedef struct CalTable CalTable;


static char*	initlsp25h(int);
static char*	inithts221(int);
static char*	initlsm9mag(int);
static char*	initlsm9gyac(int);
static char*	initled(int);
static void		initfs(void);
static void		getcal(int);
static char*	readtempp(Req*);
static char*	readpress(Req*);
static char*	readtemph(Req*);
static char*	readhumid(Req*);
static char*	readaccel(Req*);
static char*	readgyro(Req*);
static char*	readmag(Req*);
static char*	writeled(Req*);
static void		rstart(Srv*);
static void		ropen(Req*);
static void		rread(Req*);
static void		rwrite(Req*);
static void		rend(Srv*);


struct Devfile {
	char	*name;
	char*	(*rread)(Req*);
	char*	(*rwrite)(Req*);
	int		mode;
};


struct CalTable {
	int h0rh;
	int h1rh;
	int h0out;
	int h1out;
	int t0degc;
	int t1degc;
	int t0out;
	int t1out;
};


Devfile files[] = {
	{ "tempp", readtempp, nil, DMEXCL|0444 },
	{ "press", readpress, nil, DMEXCL|0444 },
	{ "temph", readtemph, nil, DMEXCL|0444 },
	{ "humid", readhumid, nil, DMEXCL|0444 },
	{ "accel", readaccel, nil, DMEXCL|0444 },
	{ "gyro", readgyro, nil, DMEXCL|0444 },
	{ "mag", readmag, nil, DMEXCL|0444 },
	{ "led", nil, writeled, DMEXCL|0222 },
};


Srv s = {
	.start = rstart,
	.open = ropen,
	.read = rread,
	.write = rwrite,
	.end = rend,
};


CalTable cal;
int debug;


static char*
initlsp25h(int on)
{
	uchar buf[2];
	int fd;

	fd = open("/dev/i2c1/i2c.5c.data", ORDWR);
	if(fd < 0)
		return("open lsp25h fail");

	if(on){
		/* ctrl_reg1, power on, 1Hz */
		buf[0] = 0x20;
		buf[1] = 0x90;
		pwrite(fd, buf, 2, 0);

		/* ctrl_reg2, boot */
		buf[0] = 0x21;
		buf[1] = 0x80;
		pwrite(fd, buf, 2, 0);

		/* this sleep is to give time for */
		/* the sensors to store a value,  */
		/* else it defaults to            */
		/* 42.5c                          */
		/* 760 hPa                        */

		sleep(100);

		if(debug)
			print("init lsp25h %d\n", fd);

		close(fd);
		return nil;
	} else {
		buf[0] = 0x20;
		buf[1] = 0x00;
		pwrite(fd, buf, 2, 0);
		if(debug)
			print("close lsp25h %d\n", fd);
		close(fd);
	}

	return nil;
}


static char*
inithts221(int on)
{
	uchar buf[2];
	int fd;

	if(debug)
		print("init hts221...");

	fd = open("/dev/i2c1/i2c.5f.data", ORDWR);
	if(fd < 0)
		return("open hts221 fail");

	if(on){
		/* av_conf reg, 1B=00011011 */
		buf[0] = 0x10;
		buf[1] = 0x1B;
		pwrite(fd, buf, 2, 0);

		/* ctrl_reg1, power up, 1Hz */
		buf[0] = 0x20;
		buf[1] = 0x81;
		pwrite(fd, buf, 2, 0);

		/* ctrl_reg2, boot, enable one-shot*/
		buf[0] = 0x21;
		buf[1] = 0x81;
		pwrite(fd, buf, 2, 0);

		/* go fetch the calibration data */
		getcal(fd);

		if(debug)
			print("done %d\n", fd);

		close(fd);
		return nil;
	} else {
		buf[0] = 0x20;
		buf[1] = 0x00;
		pwrite(fd, buf, 2, 0);
		if(debug)
			print("close hts221 %d\n", fd);
		close(fd);
	}

	return nil;
}


static char*
initlsm9mag(int on)
{
	uchar buf[2];
	int fd;

	fd = open("/dev/i2c1/i2c.1c.data", ORDWR);
	if(fd < 0)
		return("open lsm9mag fail");

	if(on){
		/* ctrl_reg1_m, high performance, 10Hz */
		buf[0] = 0x20;
		buf[1] = 0x50;
		pwrite(fd, buf, 2, 0);

		/* ctrl_reg2_m, default scale */
		buf[0] = 0x21;
		buf[1] = 0x00;
		pwrite(fd, buf, 2, 0);

		/* ctrl_reg3_m, power on, continuous mode */
		buf[0] = 0x22;
		buf[1] = 0x00;
		pwrite(fd, buf, 2, 0);

		/* ctrl_reg4_m, high performance Z axis */
		buf[0] = 0x23;
		buf[1] = 0x08;
		pwrite(fd, buf, 2, 0);

		if(debug)
			print("init lsm9mag %d\n", fd);

		close(fd);
		return nil;
	} else {
		/* send power down command */
		buf[0] = 0x22;
		buf[1] = 0x03;
		pwrite(fd, buf, 2, 0);
		if(debug)
			print("close lsm9mag %d\n", fd);
		close(fd);
	}

	return nil;
}


static char*
initlsm9gyac(int on)
{
	uchar buf[2];
	int fd;

	fd = open("/dev/i2c1/i2c.6a.data", ORDWR);
	if(fd < 0)
		return("open lsm9gyac fail");

	if(on){
		/* for accel, ctrl_reg6_xl, 119Hz refresh */
		buf[0] = 0x20;
		buf[1] = 0x60;
		pwrite(fd, buf, 2, 0);

		/* for gyro, ctrl_reg4, 119Hz, 500pdi, default BW */
		buf[0] = 0x10;
		buf[1] = 0x68;
		pwrite(fd, buf, 2, 0);

		/* for gyro, ctrl_reg4, enable gyro x, y, z */
		buf[0] = 0x1E;
		buf[1] = 0x38;
		pwrite(fd, buf, 2, 0);

		if(debug)
			print("init lsm8ag %d\n", fd);

		close(fd);
		return nil;
	} else {
		/* power down accel */
		buf[0] = 0x20;
		buf[1] = 0x00;
		pwrite(fd, buf, 2, 0);
		if(debug)
			print("close lsm9gyac %d\n", fd);
		close(fd);
	}

	return nil;
}


static char*
initled(int on)
{
	int fd;
	uchar buf[193];

	fd = open("/dev/i2c1/i2c.46.data", ORDWR);
	if(fd < 0)
		return("open led fail");


	if(on){
		if(debug)
			print("init led %d\n", fd);

	} else {
		if(debug)
			print("close led %d\n", fd);
	}

	/* full clear from stat taks 193 bytes for some reason */
	memset(buf, 0, 193);
	pwrite(fd, buf, 193, 0);

	return nil;
}


static void
initfs(void)
{
	char *user, *err, *dirname;
	int i;
	File *root;
	File *devdir;

	user = getuser();
	dirname = "shat";

	s.tree = alloctree(user, user, 0555, nil);
	if(s.tree == nil)
		sysfatal("initfs: alloctree: %r");

	root = s.tree->root;

	if((devdir = createfile(root, dirname, user, DMDIR|0555, nil)) == nil)
		sysfatal("initfs: createfile: scd40: %r");

	for(i = 0; i < nelem(files); i++){
		if(createfile(devdir, files[i].name, user, files[i].mode, files + i) == nil)
			sysfatal("initfs: createfile: %s: %r", files[i].name);
	}
}


static void
getcal(int fd)
{

/*
 * This is all to fetch and assemble
 * the calabration data needed to
 * get usable info out of the actual
 * temperature and humidity outputs
 */

	uchar reg[1], buf[1], tmp[1];

	if(debug)
		print("init caltable...");

	reg[0] = 0x30;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	cal.h0rh = buf[0] / 2;

	reg[0] = 0x31;
	pwrite(fd, reg, 1, 0);
	pread(fd , buf, 1, 0);
	cal.h1rh = buf[0] / 2;

	reg[0] = 0x35;
	pwrite(fd, reg, 1, 0);
	pread(fd, tmp, 1, 0);

	reg[0] = 0x32;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	cal.t0degc = buf[0] | ((tmp[0] & 0x3) << 8);
	cal.t0degc = cal.t0degc / 8;

	reg[0] = 0x33;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	cal.t1degc = buf[0] | ((tmp[0] & 0xC) << 6);
	cal.t1degc = cal.t1degc / 8;

	reg[0] = 0x36;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	reg[0] = 0x37;
	pwrite(fd, reg, 1, 0);
	pread(fd, tmp, 1, 0);
	cal.h0out = buf[0] | (tmp[0] << 8);

	reg[0] = 0x3A;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	reg[0] = 0x3B;
	pwrite(fd, reg, 1, 0);
	pread(fd, tmp, 1, 0);
	cal.h1out = buf[0] | (tmp[0] << 8);

	reg[0] = 0x3C;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	reg[0] = 0x3D;
	pwrite(fd, reg, 1, 0);
	pread(fd, tmp, 1, 0);
	cal.t0out = buf[0] | (tmp[0] << 8);

	reg[0] = 0x3E;
	pwrite(fd, reg, 1, 0);
	pread(fd, buf, 1, 0);
	reg[0] = 0x3F;
	pwrite(fd, reg, 1, 0);
	pread(fd, tmp, 1, 0);
	cal.t1out = buf[0] | (tmp[0] << 8);

	if(debug)
		print("read...");

	if(cal.h0out > 32767)
		cal.h0out -= 65536;

	if(cal.h1out > 32767)
		cal.h1out -= 65536;

	if(cal.t0out > 32767)
		cal.t0out -= 65536;

	if(cal.t1out > 32767)
		cal.t0out -= 65536;

	if(debug)
		print("got \n%d\n%d\n%d\n%d\n%d\n%d\n%d\n%d\n", cal.h0rh, cal.h1rh, cal.h0out, cal.h1out, cal.t0degc, cal.t1degc, cal.t0out, cal.t1out);
}


static char*
readtempp(Req *r)
{
	char out[8];
	uchar reg[1], tempol[1], tempoh[1];
	int fd, temp;

	fd = open("/dev/i2c1/i2c.5c.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	/* get temperature data */
	reg[0] = 0x2B;
	pwrite(fd, reg, 1, 0);
	pread(fd, tempol, 1, 0);

	reg[0] = 0x2C;
	pwrite(fd, reg, 1, 0);
	pread(fd, tempoh, 1, 0);

	/* do the math */
	temp = tempol[0] | (tempoh[0] << 8);
	if (temp > 32767) temp -= 65536;
	temp = 425 + (temp / 48);

	sprint(out, "%d.%d\n", temp/10, temp%10);

	close(fd);

	readstr(r, out);
	return nil;
}


static char*
readpress(Req *r)
{
	char out[8];
	uchar reg[1], presoxl[1], presol[1], presoh[1];
	int fd, press;

	fd = open("/dev/i2c1/i2c.5c.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	/* get pressure data */
	reg[0] = 0x28;
	pwrite(fd, reg, 1, 0);
	pread(fd, presoxl, 1, 0);

	reg[0] = 0x29;
	pwrite(fd, reg, 1, 0);
	pread(fd, presol, 1, 0);

	reg[0] = 0x2A;
	pwrite(fd, reg, 1, 0);
	pread(fd, presoh, 1, 0);

	/* do the math */
	press = presoxl[0] | (presol[0] << 8) | (presoh[0] << 16);
	press = press / 4096;

	sprint(out, "%d\n", press);

	close(fd);

	readstr(r, out);
	return nil;
}


static char*
readtemph(Req *r)
{
	uchar reg[1], low[1], high[1];
	int tout, hout, fd;
	char out[8];
	float temp, foo, bar;

	fd = open("/dev/i2c1/i2c.5f.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	reg[0] = 0x2A;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x2B;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	tout = low[0] | (high[0] << 8);

	if(tout > 32767)
		tout -= 65536;

	foo = (tout - cal.t0out) * (cal.t1degc - cal.t0degc);
	bar = (cal.t1out - cal.t0out);
	temp = foo / bar + cal.t0degc;

//	temp = (((tout - cal.t0out) * (cal.t1degc - cal.t0degc)) / (cal.t1out - cal.t0out) + cal.t0degc);

	sprint(out, "%.1f\n", temp);

	close(fd);
	readstr(r, out);
	return nil;
}


static char*
readhumid(Req *r)
{
	uchar reg[1], low[1], high[1];
	int tout, hout, fd;
	char out[8];
	float humid, foo, bar;

	fd = open("/dev/i2c1/i2c.5f.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	reg[0] = 0x28;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x29;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	hout = low[0] | (high[0] << 8);

	if(hout > 32767)
		hout -= 65536;

	foo = (hout - cal.h0out) * (cal.h1rh -  cal.h0rh);
	bar = (cal.h1out - cal.h0out);
	humid = foo / bar + cal.h0rh;

//	humid = ((hout - cal.h0out) * (cal.h1rh -  cal.h0rh)) / (cal.h1out - cal.h0out) + cal.h0rh;

	sprint(out, "%.1f\n", humid);

	close(fd);
	readstr(r, out);
	return nil;
}


static char*
readaccel(Req *r)
{
	char out[128];
	int fd, o, x, y, z;
	uchar reg[1], low[1], high[1];

	fd = open("/dev/i2c1/i2c.6a.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	/* X axis */
	reg[0] = 0x28;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x29;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32757)
		o -= 65536;

	x = o;

	/* Y axis */
	reg[0] = 0x2A;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x2B;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32757)
		o -= 65536;

	y = o;

	/* Z axis */
	reg[0] = 0x2C;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x2D;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32757)
		o -= 65536;

	z = o;

	sprint(out, "%d %d %d\n", x, y, z);

	close(fd);
	readstr(r, out);
	return nil;
}


static char*
readgyro(Req *r)
{
	char out[128];
	int fd, o, x, y, z;
	uchar reg[1], low[1], high[1];

	fd = open("/dev/i2c1/i2c.6a.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	/* X axis */
	reg[0] = 0x18;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x19;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32757)
		o -= 65536;

	x = o;

	/* Y axis */
	reg[0] = 0x1A;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x1B;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32757)
		o -= 65536;

	y = o;

	/* Z axis */
	reg[0] = 0x1C;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x1D;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32757)
		o -= 65536;

	z = o;

	sprint(out, "%d %d %d\n", x, y, z);

	close(fd);
	readstr(r, out);
	return nil;
}


static char*
readmag(Req *r)
{
	char out[128];
	uchar reg[1], low[1], high[1];
	int fd, x, y, z, o;

	fd = open("/dev/i2c1/i2c.1c.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	/* X axis */
	reg[0] = 0x28;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x29;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32767)
		o -= 65536;

	x = o;

	/* Y axis */
	reg[0] = 0x2A;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x2B;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32767)
		o -= 65536;

	y = o;

	/* Z axis */
	reg[0] = 0x2C;
	pwrite(fd, reg, 1, 0);
	pread(fd, low, 1, 0);

	reg[0] = 0x2D;
	pwrite(fd, reg, 1, 0);
	pread(fd, high, 1, 0);

	o = low[0] | (high[0] << 8);

	if(o > 32767)
		o -= 65536;

	z = o;

	sprint(out, "%d %d %d\n", x, y, z);

	close(fd);
	readstr(r, out);
	return nil;
}


static char*
writeled(Req *r)
{
	int fd;
	uchar buf[192];

	fd = open("/dev/i2c1/i2c.46.data", ORDWR);
	if(fd < 0)
		return("open I²C fail");

	memmove(buf, r->ifcall.data, 192);

	pwrite(fd, buf, 192, 0);

	close(fd);
	return nil;
}


static void
rstart(Srv*)
{
	char *err;

	if(err = initlsp25h(1))
		print(err);

	if(err = inithts221(1))
		print(err);

	if(err = initlsm9mag(1))
		print(err);

	if(err = initlsm9gyac(1))
		print(err);

	if(err = initled(1))
		print(err);

	initfs();
}


static void
ropen(Req *r)
{
	respond(r, nil);
}


static void
rread(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->rread(r));
}


static void
rwrite(Req *r)
{
	Devfile *f;

	r->ofcall.count = r->ifcall.count;
	f = r->fid->file->aux;
	respond(r, f->rwrite(r));
}


static void
rend(Srv*)
{
	initlsp25h(0);
	inithts221(0);
	initlsm9mag(0);
	initlsm9gyac(0);
	initled(0);

	postnote(PNGROUP, getpid(), "shutdown");
	threadexitsall(nil);
}


void
usage(void)
{
	fprint(2, "usage: %s [-m mtpt] [-s service]\n", argv0);
	exits("usage");
}


void
threadmain(int argc, char *argv[])
{
	char *srvname, *mtpt, *addr;

	mtpt = "/mnt";
	srvname = "shatfs";

	ARGBEGIN {
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'd':
		debug++;
		break;
	default:
		usage();
	} ARGEND;

	threadpostmountsrv(&s, srvname, mtpt, MBEFORE);
	threadexits(nil);
}
