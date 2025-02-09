#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char* argv[]) {


    openlog(NULL, 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Error function must have <writefile> and <writestr> as given arguments");
        closelog();
        exit(1);
    }

    char* writefile = argv[1];
    char* writestr = argv[2];
    

    FILE *myfile = fopen(writefile, "w");


    if(myfile == NULL) {
        syslog(LOG_ERR, "Error message: %i", errno );
        closelog();
        exit(1);
    }

    fputs(writestr, myfile);

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);


    fclose(myfile);
    closelog();

    return 0;
}