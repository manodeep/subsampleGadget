#include <assert.h>
#include "gadget_utils.h"
#include "utils.h"

FILE * position_file_pointer(const char *file, const int type, const enum iofields field)
{
  FILE *fp = my_fopen(file,"r");
  long bytes = 0;
  size_t id_bytes = get_gadget_id_bytes(file);
  struct io_header header = get_gadget_header(file);
  int64_t nwithmass[6] = {0};
  int64_t totnwithmass = 0;

  switch(field)
	{
	case IO_POS:
	  bytes += 4 + sizeof(struct io_header) + 4;
	  bytes += 4;
	  for(int k=0;k<type;k++) {
		bytes += sizeof(float)*header.npart[k]*3 ;
	  }
	  break;
	  
	case IO_VEL:
	  bytes += 4 + sizeof(struct io_header) + 4;
	  bytes += 4;
	  for(int k=0;k<6;k++) {
		bytes += sizeof(float)*header.npart[k]*3 ;
	  }
	  bytes += 4;
	  //All of the positions are accounted for. 

	  bytes += 4; //for the padding for velocity
	  for(int k=0;k<type;k++) {
		bytes += sizeof(float)*header.npart[k]*3;
	  }
	  break;
	  
	case IO_ID:
	  bytes += 4 + sizeof(struct io_header) + 4;
	  bytes += 4;
	  for(int k=0;k<6;k++) {
		bytes += sizeof(float)*header.npart[k]*3 ;
	  }
	  bytes += 4;
	  //All of the positions are accounted for. 

	  //Account for all of the velocities
	  bytes += 4; 
	  for(int k=0;k<6;k++) {
		bytes += sizeof(float)*header.npart[k]*3;
	  }
	  bytes += 4;
	  
	  bytes += 4;
	  for(int k=0;k<type;k++) {
		bytes += id_bytes*header.npart[k];
	  }
	  break;

	  /* The mass field may not exist -> return NULL */
	case IO_MASS:
	  for(int k=0;k<6;k++){
		if(header.mass[k] == 0.0) {
		  nwithmass[k] += header.npart[k];
		  totnwithmass += header.npart[k];
		}
	  }
	  if(totnwithmass == 0) {
		fclose(fp);
		return NULL;
	  } else {
		bytes += 4 + sizeof(struct io_header) + 4;
		bytes += 4;
		for(int k=0;k<6;k++) {
		  bytes += sizeof(float)*header.npart[k]*3 ;
		}
		bytes += 4;
		//All of the positions are accounted for. 
		
		//Account for all of the velocities
		bytes += 4; 
		for(int k=0;k<6;k++) {
		  bytes += sizeof(float)*header.npart[k]*3;
		}
		bytes += 4;
		
		//Account for the ids
		bytes += 4;
		for(int k=0;k<6;k++) {
		  bytes += id_bytes*header.npart[k];
		}
		bytes += 4;


		//Now skip the particles with mass
		bytes +=4;
		for(int k=0;k<type;k++) {
		  bytes += sizeof(float)*nwithmass[k];
		}
	  }
	  break;

	default:
	  fprintf(stderr,"ERROR:IO_FIELD = %d is not implemented\n",field);
	  exit(EXIT_FAILURE);
	}
  my_fseek(fp, bytes, SEEK_CUR);
  return fp;
}


size_t get_gadget_id_bytes(const char *file)
{
  struct io_header header = get_gadget_header(file);
  long bytes;
  int dummy;
  int64_t totnpart=0;
  size_t id_bytes=0;
  FILE *fp = my_fopen(file,"r");
  for(int k=0;k<6;k++) {
	totnpart += header.npart[k];
  }
  assert(totnpart > 0 && "There exist particles in the snapshot file");
  
  bytes =  4 + sizeof(struct io_header)  + 4;
  bytes += 4 + sizeof(float)*3*totnpart  + 4;
  bytes += 4 + sizeof(float)*3*totnpart  + 4;
  my_fseek(fp, bytes, SEEK_CUR);
  my_fread(&dummy, sizeof(dummy), 1, fp);
  fclose(fp);
  id_bytes = dummy/totnpart;
  assert((id_bytes == 4 || id_bytes == 8 ) && "ID bytes are 4 or 8 bytes");
  return id_bytes;
}


//assumes Gadget snapshot format=1
struct io_header get_gadget_header(const char *fname)
{
  FILE *fp=NULL;
  char buf[MAXLEN], buf1[MAXLEN];
  int dummy;
  struct io_header header;
  my_snprintf(buf,MAXLEN, "%s.%d", fname, 0);
  my_snprintf(buf1,MAXLEN, "%s", fname);
  fp = fopen(buf,"r");
  if(fp == NULL) {
    fp = fopen(buf1,"r");
    if(fp == NULL) {
      fprintf(stderr,"ERROR: Could not find snapshot file.\n neither as `%s' nor as `%s'\n",buf,buf1);
      fprintf(stderr,"exiting..\n");
      exit(EXIT_FAILURE);
    }
  }

  //// Don't really care which file actually succeeded (as long as one, buf or buf1, is present)
  fread(&dummy, sizeof(dummy), 1, fp);
  fread(&header, sizeof(header), 1, fp);
  fread(&dummy, sizeof(dummy), 1, fp);
  fclose(fp);
  return header;
}

int get_gadget_nfiles(const char *fname)
{
  FILE *fp=NULL;
  char buf[MAXLEN], buf1[MAXLEN];
  int dummy;
  struct io_header header;
  my_snprintf(buf,MAXLEN, "%s.%d", fname, 0);
  my_snprintf(buf1,MAXLEN, "%s", fname);

  fp = fopen(buf,"r");
  if(fp == NULL) {
    fp = fopen(buf1,"r");
    if(fp == NULL) {
      fprintf(stderr,"ERROR: Could not find snapshot file.\n neither as `%s' nor as `%s'\n",buf,buf1);
      fprintf(stderr,"exiting..\n");
      exit(EXIT_FAILURE);
    }
  }

  //// Don't really care which file actually succeeded (as long as one, buf or buf1, is present)
  fread(&dummy, sizeof(dummy), 1, fp);
  fread(&header, sizeof(header), 1, fp);
  fread(&dummy, sizeof(dummy), 1, fp);
  fclose(fp);
  return header.num_files;
}


int64_t get_Numpart(struct io_header *header)
{
  int64_t N=0;
  if(header->num_files <= 1)
    for(int i = 0; i < 6; i++)
      header->npartTotal[i] = header->npart[i];

  for(int i = 0; i < 6; i++) {
    N += header->npartTotal[i];
    N += (((int64_t) header->npartTotalHighWord[i]) << 32);
  }

  return N;
}
