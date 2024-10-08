extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int main() {
   int a = 0;

   for (a = 0; a < 10; a = a + 1) ;
 
   PRINT(a);
}
// kate: indent-mode cstyle; indent-width 1; replace-tabs on; 
