all: music in_dummy.so in_mpd.so

clean:
	rm -f -- *.o *.so music

music: music.c music.h music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -rdynamic -o $@ $< -ldl -lpthread

in_mpd.so: in_mpd.o libmpdclient.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $^ -lpthread

in_mpd.o: in_mpd.c libmpdclient.h music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

libmpdclient.o: libmpdclient.c libmpdclient.h music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.so: %.c music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -shared -o $@ $< -lpthread
