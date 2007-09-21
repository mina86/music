all: music in_dummy.so in_mpd.so out_http.so

clean:
	rm -f -- *.o *.so music sha1



music: music.o music-impl.o dispatcher.o
	$(CC) $(CFLAGS) $(CPPFLAGS) ${LDFLAGS} -rdynamic -o $@ $^ -ldl -lpthread

music.o: music.c music.h music-int.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

music-impl.o: music-impl.c music.h music-int.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

dispatcher.o: dispatcher.c music.h music-int.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<



in_mpd.so: in_mpd.o libmpdclient.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $^ -lpthread

in_mpd.o: in_mpd.c libmpdclient.h music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

libmpdclient.o: libmpdclient.c libmpdclient.h music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<



out_http.so: out_http.o sha1.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $^ -lcurl



%.so: %.c music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $< -lpthread

%.o: %.c music.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<



sha1: sha1.c sha1.h config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -DSHA1_COMPILE_TEST -o $@ $< -lcrypto
