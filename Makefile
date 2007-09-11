all: music in_dummy.so in_mpd.so

clean:
	rm -f -- *.o *.so music sha1

music: music.c music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -rdynamic -o $@ $< -ldl -lpthread

in_mpd.so: in_mpd.o libmpdclient.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $^ -lpthread

in_mpd.o: in_mpd.c libmpdclient.h music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

libmpdclient.o: libmpdclient.c libmpdclient.h music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.so: %.c music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $< -lpthread

sha1: sha1.c sha1.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -DSHA1_COMPILE_TEST -shared -o $@ $< -lcrypto
