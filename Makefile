all: music in_dummy.so in_mpd.so

clean:
	rm -f -- *.o *.so music

music: music.c music.h music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -ldl $(LDFLAGS) -rdynamic -o $@ $<
# The "-ldl" argument above must be put before $(LDFLAGS) because if
# user has -Wl,--as-needed there strange error occur (that is linker
# reports undefined refenreces to dlopen, dlerror, dlsym and dlclose).
# Need to ask dozzie why. :] (In fact users shouldn't have --as-needed
# flag I guess.)

in_mpd.so: in_mpd.o libmpdclient.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -lpthread -shared -o $@ $^

in_mpd.o: in_mpd.c libmpdclient.h music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

libmpdclient.o: libmpdclient.c libmpdclient.h music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.so: %.c music.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -lpthread -shared -o $@ $<
