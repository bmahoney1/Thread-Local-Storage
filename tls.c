#include "tls.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#define HASH_SIZE 128
#define _GNU_SOURCE

// This struct for the the pages
struct page{
	unsigned long int address;
    	int ref_count;		
};

// This struct is for the thread local storage
typedef struct thread_local_storage{
    	unsigned int size;      
    	unsigned int page_num;  
    	struct page** pages;   
}TLS;


// This struct is going to be used to keep track of all the storages globally
struct hash_element{
    	pthread_t tid;    
    	TLS* tls;    
    	struct hash_element *next;  
};

struct hash_element* hash_table[HASH_SIZE]; // This makes a table the size of 128 for all the storages     
unsigned long int page_size = 0; // This will be used later to hold the page size 
pthread_mutex_t mutex;
int initialized = 0; // This is what is used to make sure the tls_init function is only called the first time


// The given protect functions to protect and uprotect the pages
static void tls_protect(struct page* p){
    if(mprotect((void *) p->address, page_size, PROT_NONE)){
        fprintf(stderr, "tls_protect: could not protect page\n");
        exit(1);
    }
}

static void tls_unprotect(struct page* p, const int protect){
    if(mprotect((void *) p->address, page_size, protect)){
        fprintf(stderr, "tls_unprotect: could not unprotect page\n");
        exit(1);
    }
}

// This function checks to see if the page fault was occured 
static void tls_handle_page_fault(int sig, siginfo_t *si, void *context){

    unsigned long int p_fault = ((unsigned long int) si->si_addr & ~(page_size - 1));
	int i;

	int j;
    for(i = 0; i < HASH_SIZE; i++){
        if(hash_table[i] != NULL){ // This checks to see if there are any elements in this index
            struct hash_element* element = hash_table[i];
        
            while(element!= NULL){ // This goes down the linked list to check each element linked to the index
                for(j = 0; j < element->tls->page_num; j++){
                    
                    if(element->tls->pages[j]->address == p_fault){ // This checks to see if this page is the address of the page fault
                        pthread_exit(NULL);
                    }
                }
                element = element->next; // Moves the pointer to the next element in the linked list at this index
            }
        }
    }

    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
}

// This function initializes the signals
static void tls_init(){
	   
 	struct sigaction sigact;
    	
	page_size = getpagesize(); // This gets the page size

    	sigemptyset(&sigact.sa_mask);
    	sigact.sa_flags = SA_SIGINFO;
    
    	sigact.sa_sigaction = tls_handle_page_fault;
    	sigaction(SIGBUS, &sigact, NULL);
    	sigaction(SIGSEGV, &sigact, NULL);

	initialized = 1; // This makes it so that this function won't run again

}

// This function creates the thread local storage
int tls_create(unsigned int size){

	if(initialized != 1){
		tls_init(); // Initializes
	}

	pthread_t TID = pthread_self(); // gets the thread id
	
	int index = TID%HASH_SIZE; // This gets the index for the hash table
	struct hash_element* element1 = hash_table[index]; // sets the element to the first element in the linked list for this index
        if(hash_table[index] != NULL){ // Checks to see if any elements belong to the index
        	//element1 = hash_table[index];

                while(element1 != NULL){ // this loop now iterate through the linked list to find the correct element for the tid
                        if(element1->tid == TID){
                                break;
                        }
            		element1 = element1->next;
        }
    }

      	
	// checks to make sure it is a valid size and that the element does not already have a tls	
	if(size <= 0 || element1 != NULL){
		return -1;
	}

	TLS* tls = (TLS*) calloc(1,sizeof(TLS));
	tls->size = size;
	// checking to see if an additional page is required
	int additional_page = 0;
	if(size%page_size != 0){
		additional_page = 1;
	}

	tls->page_num = size / page_size + additional_page; // this gets the total number of pages needed for the element
	int i;

	tls->pages = (struct page**) calloc(tls->page_num, sizeof(struct page*));// this dynamically allocates enough memory for all pages
	// this goes through and uses mmap to get memory for each of the pages 
	for( i = 0; i < tls->page_num; i++){
		tls->pages[i] = (struct page*) calloc(1,sizeof(struct page));
                tls->pages[i]->address = (unsigned long int) mmap(0, page_size, PROT_NONE, MAP_ANON | MAP_PRIVATE, 0, 0);


                if(tls->pages[i]->address == (unsigned long int) MAP_FAILED){
                        return -1;
                }

                tls->pages[i]->ref_count = 1;
	}

	struct hash_element* element = (struct hash_element*) malloc(sizeof(struct hash_element));
	element->tls = tls;
	element->tid = TID;
	// this adds the element to the hash table, either starting the linked list at the index or adding to it
        if(hash_table[index] != NULL){
                struct hash_element* oldelement = hash_table[index]; // this stores the original tls
                hash_table[index] = element; // this now points to the new tls
                hash_table[index]->next = oldelement; // now the linked list is connected with all elements

      		return 0;
        }else if(hash_table[index] == NULL){ // for the case that this is the first instance
                hash_table[index] = element;
                hash_table[index]->next = NULL;

                return 0;
        }else{

    		return -1;
	}
	
}
// this gets rid of the element from the table and frees the memory
int tls_destroy(){

	pthread_t TID = pthread_self();

	int index = TID%HASH_SIZE;
	struct hash_element* element;

    	if(hash_table[index] != NULL){ // this proceeds as long as there is at least one element linked to the index
        	struct hash_element* temp = NULL;
        	element = hash_table[index];


                while(element != NULL){
                        if(element->tid == TID){
                                break;
                        }
            		temp = element;
            		element = element->next;
                }

                if(temp != NULL){
                        temp = element->next;
                }
    	}else{
        	return -1;
    	}


	if(element == NULL){
		return -1;
	}
	int i;
	// this goes through and frees each individual page	
	for( i = 0; i < element->tls->page_num; i++){
		if(element->tls->pages[i]->ref_count == 1){
			munmap((void *) element->tls->pages[i]->address, page_size);
			free(element->tls->pages[i]);
		}
		else{
			(element->tls->pages[i]->ref_count)--;
		}
	}
	// this frees all pages, the tls and the element
	free(element->tls->pages);

	free(element->tls);
	free(element);
	
	return 0;
}
int tls_write(unsigned int offset, unsigned int length, char *buffer){

	pthread_t TID = pthread_self();
	int index1 = TID%HASH_SIZE;

	struct hash_element* element = hash_table[index1];
	// This goes through and gets the correct element 
        if(hash_table[index1] != NULL){
        	//element = hash_table[index1];

                while(element != NULL){ // this is going to check each element in the linked list for this index
                        if(element->tid == TID){
                                break;
                        }
            		element = element->next;
        	}
		//element = NULL;
    	}

	int i;
	if(element == NULL){
		return -1;
	}
		
	
	if(element->tls->size < (offset+length)){
                return -1;
        }	
	
	for(i = 0; i < element->tls->page_num; i++){
		tls_unprotect(element->tls->pages[i], PROT_WRITE);
	}
	
	// this was the code given to go through and write the data to the page and to copy on write if there were more than 1 pointer
	unsigned int cnt, idx;
	for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
 		struct page *p, *copy;
 		unsigned int pn, poff;
 		pn = idx / page_size;
 		poff = idx % page_size;
 		p = element->tls->pages[pn];
 		if (p->ref_count > 1) {
 			copy = (struct page *) calloc(1, sizeof(struct page));
 			copy->address = (unsigned long int) mmap(0,page_size, PROT_WRITE,MAP_ANON | MAP_PRIVATE, 0, 0);
 			copy->ref_count = 1;
 			element->tls->pages[pn] = copy;
 			memcpy((void*) copy->address, (void*)p->address, page_size);
			/* update original page */
 			p->ref_count--;
 			tls_protect(p);
 			p = copy; // now it is only writing to the copied page
 		}
 		char *dst = (char *) (p->address + poff);
		*dst = buffer[cnt];
 	}
	
	for( i = 0; i < element->tls->page_num; i++){
		tls_protect(element->tls->pages[i]);
	}


	return 0;

}

int tls_read(unsigned int offset, unsigned int length, char *buffer){
	pthread_t TID = pthread_self();
	
	int index1 = TID%HASH_SIZE;
	// this goes through and gets the correct element for the tid
	struct hash_element* element;
        if(hash_table[index1] != NULL){
                element = hash_table[index1]; // set the element here so we do not mess with the global linked list

                while(element != NULL){
                        if(element->tid == TID){
                                break;
                        }
                        element = element->next;
        }

    }else{
		return -1;
	}   
	

	int i;
	if(element == NULL){
		return -1;
	}else if((offset + length) > element->tls->size){
		return -1;
	}


	for( i = 0; i < element->tls->page_num; i++){
		tls_unprotect(element->tls->pages[i], PROT_READ);

	}
	int count = 0;
	int index = 0;
	for(index = offset; index < (offset + length); ++index){
		struct page* page;

		unsigned int pn = index / page_size;
		unsigned int poff = index % page_size;

		page = element->tls->pages[pn];
		
		char *src = ((char *) page->address) + poff;
		buffer[count] = *src;

		count++;
	}

	for( i = 0; i < element->tls->page_num; i++){
		tls_protect(element->tls->pages[i]);
	}


	return 0;
}

int tls_clone(pthread_t tid){
	pthread_t clone_tid = pthread_self();
	int i;
	
	int index = clone_tid%HASH_SIZE;
	// this is making sure it does not already have a tls
	struct hash_element* clone;
        if(hash_table[index] != NULL){
                clone = hash_table[index];

                while(clone != NULL){
                        if(clone->tid == clone_tid){
                                break;
                        }
                        clone = clone->next;
        }
    }   


	int index2 = tid%HASH_SIZE;
	// this gets the element that already has a tls to then use it for the clone
	struct hash_element* target;
        if(hash_table[index2] != NULL){
                target = hash_table[index2];

                while(target != NULL){
                        if(target->tid == tid){
                                break;
                        }
                        target = target->next;
        }
    }else{
		return -1;
	}  

	if(clone != NULL || target == NULL){
		return -1;
	}


	clone = (struct hash_element*) calloc(1,sizeof(struct hash_element)); // allocating memory for the clone element 
	clone->tid = clone_tid;

	clone->tls = (TLS*) calloc(1,sizeof(TLS)); // allocating memory for the clone tls

	// copying data over
	clone->tls->size = target->tls->size;

	clone->tls->page_num = target->tls->page_num; 
	
	clone->tls->pages = (struct page**) calloc(clone->tls->page_num, sizeof(struct page*));
	
	for(i = 0; i < clone->tls->page_num; i++){
		clone->tls->pages[i] = target->tls->pages[i]; // having them point to the same place
		(clone->tls->pages[i]->ref_count)++;
	}
	// adding the clone to the linked list
	if(hash_table[index] != NULL){
                struct hash_element* oldelement = hash_table[index];
                hash_table[index] = clone;
                hash_table[index]->next = oldelement;

                return 0;
        }else if (hash_table[index] == NULL){
                hash_table[index] = clone;
                hash_table[index]->next = NULL;

                return 0;
        }else{

    		return -1;
	}
}
