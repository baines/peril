
peril: main.o ext_call.o
	gcc -g $^ -o $@ -ldl

main.o: main.c
	gcc -c -g $< -o $@

ext_call.o: ext_call.s
	as -g $< -o $@

clean:
	$(RM) main.o ext_call.o peril

.PHONY: clean
