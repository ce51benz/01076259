default: service client

service: codegen service.c
	gcc -Wall `pkg-config --cflags --libs glib-2.0,gio-unix-2.0` service.c rfos.c -lpthread -lm -D_LARGEFILE64_SOURCE -o rfos-svc

client: codegen client.c
	gcc -Wall `pkg-config --cflags --libs glib-2.0,gio-unix-2.0` client.c rfos.c -o rfos

codegen: rfos.xml
	gdbus-codegen --interface-prefix=kmitl.ce.os --generate-c-code=rfos rfos.xml

clean:
	$(RM) rfos-svc rfos
	$(RM) rfos.c rfos.h
