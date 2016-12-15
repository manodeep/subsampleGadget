/* #define _XOPEN_SOURCE 500 */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>

/* for open/close and other file io*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/*for sendfile (in-kernel file to file copy, very fast) */
#ifdef USE_SENDFILE
#include <sys/sendfile.h>
#endif

#if defined(USE_MMAP)
#include <sys/mman.h>
#elif defined(USE_SENDFILE)
#include <sys/sendfile.h>
#else
#endif


#ifdef USE_WRITEV
#ifndef USE_MMAP  
#error USE_MMAP must be enabled with USE_WRITEV
#endif
#include <sys/uio.h>
#endif

#include <gsl/gsl_rng.h>

#include "macros.h"
#include "utils.h"
#include "progressbar.h"
#include "gadget_utils.h"


/* Copied straight from https://fossies.org/dox/gsl-2.2.1/shuffle_8c_source.html*/
/* Adapted to generate array indices */
int gsl_ran_arr_index (const gsl_rng * r, size_t * dest, const size_t k, const size_t n)
{
  /* Choose k out of n items, return an array x[] of the k items.
      These items will prevserve the relative order of the original
      input -- you can use shuffle() to randomize the output if you
      wish */
 
  if (k > n) {
	fprintf(stderr,"k=%zu is greater than n=%zu. Cannot sample more than n items\n",
			k, n);
	return EXIT_FAILURE;
  }
  if(k == n) {
	for(size_t i=0;i<n;i++) {
	  dest[i] = i;
	}
  } else {
	size_t j=0;
	for (size_t i = 0; i < n && j < k; i++) {
	  if ((n - i) * gsl_rng_uniform (r) < k - j) {
		dest[j] = i;
		j++ ;
	  }
	}
  }
 
  return EXIT_SUCCESS;
}
 

int write_random_subsample_of_field(int in_fd, int out_fd, off_t in_offset, off_t out_offset, const int dest_npart, const size_t itemsize, size_t *random_indices
#ifdef USE_MMAP
									,char *in_memblock
#endif
)
{
#ifdef USE_MMAP
  in_memblock += in_offset;//These are here so that I don't lose the original pointer from mmap
#endif

#ifndef USE_MMAP
  size_t buf[itemsize];
#endif

  int interrupted=0;
  init_my_progressbar(dest_npart,&interrupted);
#ifdef USE_WRITEV
  //vector writes. Loop has to be re-written in units of IOV_MAX
  for(int i=0;i<dest_npart;i+=IOV_MAX) {
      my_progressbar(i,&interrupted);//progress-bar will be jumpy
      const int nleft = ((dest_npart - i) > IOV_MAX) ? IOV_MAX:(dest_npart - i);
      struct iovec iov[IOV_MAX];
      for(int j=0;j<nleft;j++){
          const size_t ind = random_indices[j];
          const size_t offset_in_field = ind * itemsize;
          iov[j].iov_base = in_memblock + offset_in_field;
          iov[j].iov_len = itemsize;
      }
      random_indices += nleft;
      
      ssize_t bytes_written = writev(out_fd, iov, nleft);
      XRETURN(bytes_written == nleft*itemsize, EXIT_FAILURE, "Expected to write bytes = %zu but wrote %zd instead\n",nleft*itemsize, bytes_written);
  }
#else  //Not using PWRITEV -> 3 options here i) MMAP + write ii) sendfile iii) pread + write
  for(int i=0;i<dest_npart;i++) {
	my_progressbar(i,&interrupted);
	const size_t ind = random_indices[i];
	size_t offset_in_field = ind * itemsize;
#if defined(USE_MMAP)
	ssize_t bytes_written = write(out_fd, in_memblock + offset_in_field, itemsize);
#elif defined(USE_SENDFILE)
	off_t input_offset = in_offset + offset_in_field;
	ssize_t bytes_written = sendfile(out_fd, in_fd, &input_offset, itemsize);
#else
	ssize_t bytes_read = pread(in_fd, &buf, itemsize, in_offset + offset_in_field);
	XRETURN(bytes_read == itemsize, EXIT_FAILURE, "Expected to read bytes = %zu but read %zd instead\n",itemsize, bytes_read);
	ssize_t bytes_written = write(out_fd, &buf, itemsize);
#endif//default case (pread + write)

	XRETURN(bytes_written == itemsize, EXIT_FAILURE, "Expected to write bytes = %zu but wrote %zd instead\n",itemsize, bytes_written);
	out_offset += itemsize;
  }
#endif //end of WRITEV


  finish_myprogressbar(&interrupted);
  return EXIT_SUCCESS;
}


int subsample_single_gadgetfile(const int dest_npart, const char *inputfile, const char *outputfile, const size_t id_bytes, const gsl_rng *rng, const double fraction)
{
  if(dest_npart <= 0) {
	fprintf(stderr,"Error: Desired number of particles =%d in the subsampled file must be > 0\n",dest_npart);
	return EXIT_FAILURE;
  }
  struct timespec tstart, t0, t1;
  current_utc_time(&tstart);

  int in_fd=-1;
  int out_fd=-1;
  int status = EXIT_FAILURE;
  in_fd = open(inputfile, O_RDONLY);
  if(in_fd < 0) {
	fprintf(stderr,"Error (in function %s, line # %d) while opening input file = `%s'\n",__FUNCTION__,__LINE__,inputfile);
	perror(NULL);
	return EXIT_FAILURE;
  }

#ifdef USE_MMAP
  struct stat sb;
  if(fstat(in_fd, &sb) < 0) {
	perror(NULL);
	return EXIT_FAILURE;
  }
  char *in_memblock = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, in_fd, 0);

#endif


  //Check that that the output file does not exist.
  {
	FILE *fp = fopen(outputfile,"r");
	if(fp != NULL) {
	  fclose(fp);
	  fprintf(stderr,"Warning: Output file = `%s' should not exist. "
			  "Aborting so as to avoid accidentally over-writing regular fles\n",outputfile);
	  return EXIT_FAILURE;
	}
  }

  out_fd = open(outputfile, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);//set the mode since the file is being created
  if(out_fd < 0) {
	fprintf(stderr,"Error (in function %s, line # %d) while opening output file = `%s'\n",__FUNCTION__,__LINE__, outputfile);
	perror(NULL);
	return EXIT_FAILURE;
  }

  /* Reserve disk-space for dest_npart particles, written as a fortran binary */
  const size_t pos_vel_itemsize = sizeof(float)*3;
  const off_t header_disk_size = 4 + sizeof(struct io_header) + 4;  //header
  const off_t pos_disk_size = 4 + pos_vel_itemsize*dest_npart + 4; //all 3 positions for each particle
  const off_t vel_disk_size = 4 + pos_vel_itemsize*dest_npart + 4; //all 3 velocities for each particle
  const off_t id_disk_size = 	4 + id_bytes*dest_npart + 4;//all particle IDs
  
  /* These are all of the fields to be written */
  const off_t outputfile_size = header_disk_size + pos_disk_size + vel_disk_size + id_disk_size;

  status = posix_fallocate(out_fd, 0, outputfile_size);
  if(status < 0) {
  	fprintf(stderr,"Error: Could not reserve disk space for %d particles (output file: `%s', expected file-size on disk = %zu bytes)\n",
  			dest_npart, outputfile, (size_t) outputfile_size);
  	perror(NULL);
  	return status;
  }


  struct io_header hdr;
  int dummy1=0,dummy2=0;
  const off_t pos_start_offset = header_disk_size + 4;
  const off_t vel_start_offset = header_disk_size + pos_disk_size + 4;
  const off_t id_start_offset = header_disk_size + pos_disk_size + vel_disk_size + 4;

  read(in_fd, &dummy1, 4);
  read(in_fd, &hdr, sizeof(struct io_header));
  read(in_fd, &dummy2, 4);
  if(dummy1 != 256 || dummy2 != 256) {
	fprintf(stderr,"Error: Padding bytes for header = %d (front) and %d (end) should be exactly 256\n",dummy1, dummy2);
	return EXIT_FAILURE;
  }
#ifdef USE_MMAP
  if(fraction == 1.0) {
	XRETURN(dest_npart == hdr.npart[1],EXIT_FAILURE,
			"for fraction = 1.0, input npart = %d must equal subsampled npart = %d\n",hdr.npart[1], dest_npart);
	/* XRETURN(sb.st_size == outputfile_size, EXIT_FAILURE, */
	/* 		"for fraction = 1.0, input (=%zu bytes) and output file sizes (=%zu bytes) must be the same",sb.st_size, outputfile_size); */
  }
#endif


  for(int type=0;type<6;type++) {
	if(type==1) {
	  continue;
	}

	/* There should only be dark-matter particles */
	if(hdr.npart[type] > 0) {
	  fprintf(stderr,"Error: Input file `%s' contains %d particles of type=%d. This code only works for dark-matter only simulations\n",
			  inputfile, hdr.npart[type], type);
	  return EXIT_FAILURE;
	}
  }

  /* The number of subsampled particles wanted can at most be the numbers present in the file*/
  if(dest_npart > hdr.npart[1]) {
	fprintf(stderr,"Error: Number of subsampled particles = %d exceeds the number of particles in the file = %d\n",
			dest_npart,  hdr.npart[1]);
	return EXIT_FAILURE;
  }

  struct io_header out_hdr = hdr;
  out_hdr.npart[1] = dest_npart;
  write(out_fd, &dummy1, sizeof(dummy1));
  write(out_fd, &out_hdr, sizeof(struct io_header));
  write(out_fd, &dummy2, sizeof(dummy2));
  

  //create an array of random indices
  size_t *random_indices = calloc(dest_npart, sizeof(*random_indices));
  status = gsl_ran_arr_index(rng, random_indices, (size_t) dest_npart, (size_t) hdr.npart[1]);

  //write positions
  current_utc_time(&t0);
  int posvel_disk_size=pos_vel_itemsize*dest_npart;
  write(out_fd, &posvel_disk_size, sizeof(posvel_disk_size));
  status = write_random_subsample_of_field(in_fd, out_fd, pos_start_offset, -1,dest_npart, pos_vel_itemsize, random_indices
#ifdef USE_MMAP
											   ,in_memblock
#endif
											   );
  if(status != EXIT_SUCCESS) {
	return status;
  }
  write(out_fd, &posvel_disk_size, sizeof(posvel_disk_size));
  current_utc_time(&t1);
  double pos_time = REALTIME_ELAPSED_NS(t0,t1)*1e-9;

  //write velocities
  write(out_fd, &posvel_disk_size, sizeof(posvel_disk_size));
  status = write_random_subsample_of_field(in_fd, out_fd, vel_start_offset, -1,dest_npart, pos_vel_itemsize, random_indices
#ifdef USE_MMAP
											   ,in_memblock
#endif
											   );
  if(status != EXIT_SUCCESS) {
	return status;
  }
  write(out_fd, &posvel_disk_size, sizeof(posvel_disk_size));
  current_utc_time(&t0);
  double vel_time = REALTIME_ELAPSED_NS(t1, t0)*1e-9;


  //write ids
  int id_size = id_bytes * dest_npart;
  write(out_fd, &id_size, sizeof(id_size)); 
  status = write_random_subsample_of_field(in_fd, out_fd, vel_start_offset, -1,dest_npart, id_bytes, random_indices
#ifdef USE_MMAP
											   ,in_memblock
#endif
											   );
  if(status != EXIT_SUCCESS) {
	return status;
  }
  write(out_fd, &id_size, sizeof(id_size));
  current_utc_time(&t1);
  double id_time = REALTIME_ELAPSED_NS(t0,t1)*1e-9;

  free(random_indices);

  //close the input file -> we are only reading, unlikely to be error
  close(in_fd);

#ifdef USE_MMAP
  munmap(in_memblock, sb.st_size);
#endif

  //check for error code here since disk quota might be hit
  status = close(out_fd);
  if(status != EXIT_SUCCESS){
	fprintf(stderr,"Error while closing output file = `%s'\n",outputfile);
	perror(NULL);
	return status;
  }
  current_utc_time(&t1);
  fprintf(stderr,"Done with file. Total time taken = %8.4lf seconds (pos_time = %6.3e vel_time = %6.3e id_time = %6.3e seconds)\n",REALTIME_ELAPSED_NS(tstart,t1)*1e-9,
		  pos_time,vel_time,id_time);

  return EXIT_SUCCESS;
}




int main(int argc,char **argv) 
{
  const char argnames[][100]={"fraction","input file","output file"};
  int nargs=sizeof(argnames)/(sizeof(char)*100);
  char input_filename[MAXLEN];
  char output_filename[MAXLEN];
  struct timespec tstart,t0,t1;
  int64_t nparticles_written=0,nparticles_withmass=0;
  const gsl_rng_type * rng_type = gsl_rng_ranlxd1;
  gsl_rng * rng; 
  unsigned long seed = 42;
  int64_t TotNumPart;
  current_utc_time(&tstart);

  if (argc != 4 )  {
	fprintf(stderr,"ERROR: %s usage - <fraction>  <gadget snapshot name>  <output filename>\n",argv[0]);
	fprintf(stderr,"Each file will be subsampled to get (roughly) that fraction for each particle-type\n");
    fprintf(stderr,"\nFound: %d parameters\n ",argc-1);
	int i;
    for(i=1;i<argc;i++) {
      if(i <= nargs)
		fprintf(stderr,"\t\t %s = `%s' \n",argnames[i-1],argv[i]);
      else
		fprintf(stderr,"\t\t <> = `%s' \n",argv[i]);
    }
    if(i < nargs) {
      fprintf(stderr,"\nMissing required parameters: \n");
      for(i=argc;i<=nargs;i++)
		fprintf(stderr,"\t\t %20s = `?'\n",argnames[i-1]);
    }
	fprintf(stderr,"\n\n");
    exit(EXIT_FAILURE);
  }

  double fraction = atof(argv[1]);
  XRETURN(fraction > 0 && fraction <= 1.0, EXIT_FAILURE, "Subsample fraction = %lf needs to be in (0,1]", fraction);
  strncpy(input_filename,argv[2],MAXLEN);
  strncpy(output_filename,argv[3],MAXLEN);
  XRETURN(strncmp(input_filename,output_filename,MAXLEN) != 0, EXIT_FAILURE, "Input filename = `%s' and output filename = `%s' are the same",input_filename, output_filename);
  const int nfiles = get_gadget_nfiles(input_filename);
  struct io_header header = get_gadget_header(input_filename);
  TotNumPart = get_Numpart(&header);
  XRETURN(header.npartTotal[0] == 0 && header.npartTotalHighWord[0]  == 0, EXIT_FAILURE, "Subsampling will not work with gas particles");

  fprintf(stderr,"Running `%s' on %d files with the following parameters \n",argv[0],nfiles);
  fprintf(stderr,"\n\t\t ---------------------------------------------\n");
  for(int i=1;i<=nargs;i++) {
	fprintf(stderr,"\t\t %-25s = %s \n",argnames[i-1],argv[i]);
  }
  fprintf(stderr,"\t\t ---------------------------------------------\n\n");

  //Setup the rng
  rng = gsl_rng_alloc(rng_type);
  gsl_rng_set(rng, seed);
  size_t id_bytes=0;
  int interrupted=0;
  init_my_progressbar(nfiles, &interrupted);
  for(int ifile=0;ifile<nfiles;ifile++) {
	my_progressbar(ifile,&interrupted);
	char inputfile[MAXLEN],outputfile[MAXLEN];
	my_snprintf(inputfile,MAXLEN,"%s.%d",input_filename,ifile);
	my_snprintf(outputfile, MAXLEN,"%s.%d",output_filename,ifile);
	if(ifile==0) {
	  id_bytes = get_gadget_id_bytes(inputfile);
	  fprintf(stderr,"Gadget ID bytes = %zu\n",id_bytes);
	}
	struct io_header hdr = get_gadget_header(inputfile);
	const int dest_npart = fraction * hdr.npart[1];
	XRETURN((int) (dest_npart*3*sizeof(float))  < INT_MAX, EXIT_FAILURE, 
			"Padding bytes will overflow, please reduce the value of fraction (currently, fraction = %lf)\n",fraction);
	
	int status = subsample_single_gadgetfile(dest_npart, inputfile, outputfile, id_bytes, rng, fraction);
	if(status != EXIT_SUCCESS) {
	  return status;
	}
	fprintf(stderr,"Wrote %d subsampled particles out of %d particles (%d/%d files).\n",
			dest_npart, hdr.npart[1], ifile+1,nfiles);
	interrupted=1;
  }
  finish_myprogressbar(&interrupted);

  gsl_rng_free(rng);
  current_utc_time(&t1);
  fprintf(stderr,"subsample_Gadget> Done. Wrote %"PRId64" particles to file `%s'. Time taken = %6.2lf seconds\n",
		  nparticles_written,output_filename,REALTIME_ELAPSED_NS(t1,tstart));

  return EXIT_SUCCESS;
}
