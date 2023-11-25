#include <arpa/inet.h>
//#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <string>
#include <iostream>
#include <map>
#include <fstream>
#include <iterator>
#include <vector>
#include <regex>
#include "utils.h"
#include <ctime>

#include "utils.h"
using namespace std; 
int main(){
    char filename[]= "notes.txt";
    FILE* fp1 = fopen(filename, "rb");
    cout << sizeof(ftell(fp1)) << endl;
    cout << sizeof(int) << endl;
    cout << sizeof(long int) << endl; 
    fclose(fp1);

}

void file_exp(){
    char filename[]= "notes.txt";
    char buffer[8];
    FILE* fp1 = fopen(filename, "rb");
    FILE* fp2 = fopen(filename, "rb");
    for(int i = 0; i < 4; i++){
        fread(buffer, 1,8, fp1);
        cout << buffer << endl;
    }

    fread(buffer, 1, 8, fp2);
    cout << buffer << endl;

    fseek(fp1,4,SEEK_SET);
    fread(buffer, 1,8, fp1);
    cout << buffer << endl;

    fclose(fp1);
    fclose(fp2);


    
}
//nullptr == 0     cout << (nullptr == 0);
//long int is 8 B