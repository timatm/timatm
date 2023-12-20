CC_DEFS =

all:
	gcc -g $(addprefix -D, $(CC_DEFS)) -I.. verify.c img_policy_contig.c -ltiff

so:
	gcc -g $(addprefix -D, $(CC_DEFS)) -shared -o img_placement_contig.so ./img_policy_contig.c -I.. -ltiff

clean:
	rm -f *.so *.out