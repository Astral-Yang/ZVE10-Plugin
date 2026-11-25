/* 仅用于编译期链接对齐 glibc 2.11.2 libdl.so.2 (GLIBC_2.4) 符号归属，绝不部署至目标机 */
void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
int dlclose(void *h) { (void)h; return 0; }
char *dlerror(void) { return 0; }
