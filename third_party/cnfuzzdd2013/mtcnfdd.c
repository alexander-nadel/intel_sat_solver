/* Copyright (c) 2006 - 2008, Armin Biere, Johannes Kepler University. */
/* Copyright (c) 2009, Thomas Hribernig, Johannes Kepler University. */

#define USAGE \
  "usage: mtcnfdd [-h|-t|-T <threads>|-s <value>|-mmw <value>|-mmr <value>] src dst cmd [<cmdopt> ...]\n" \
  "\n" \
  "  -h     print this command line option summary\n" \
  "  -t     thorough mode, e.g. iterate same widths multiple times\n" \
  "  -m     mask out signals when calculating exit code\n" \
  "  -e <e> set expected exit code to <e>\n" \
  "  -T <threads>  \n" \
  "  -s <start reduce width>  \n" \
  "  -mmw <minimum merge width>  \n" \
  "  -mmr <maximum merge rounds>  \n" \
  "\n" \
  "  src    file name of an existing CNF in DIMACS format\n" \
  "  dst    file name of generated minimized CNF\n" \
  "  cmd    command to debug (expects a CNF file as argument)\n" \
  "\n" \
  "The delta debugger copies 'src' to 'dst' and tries to remove\n" \
  "as many clauses and literals without changing the exit code\n" \
  "of 'cmd dst'.  Then unused variables are removed, as long the\n" \
  "exit code does not change.\n"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

//#define HELGRINDSAFE

#define TRUE INT_MAX
#define FALSE -INT_MAX

typedef struct WorkerInfo
{
	pthread_t thread;
	int id;
	int position;
	int start;
	int end;
	int ** test;
	int removed;
	int width;
	char tmp[100];
} WorkerInfo;

#define VERSION_NUMBER 2

static const char * src;
static const char * dst;
static char * cmd;

static int ** headclauses;
static int size_clauses;
static int maxidx;
static int * movedto;
static int expected;
static char tmp[100];
static int round;
static int changed;
static volatile int calls;
static int thorough;
static int masksignals;
static int threads;
static int start_reduce_width;
static int min_merge_width;
static int max_merge_rounds;
static int merge_succeeded;
static int shrink_succeeded;
static int merge_failed;
static int shrink_failed;
static WorkerInfo* workerinfo;
static pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t headclauses_mutex = PTHREAD_MUTEX_INITIALIZER;


static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** mtcnfdd: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  if (tmp[0] == '/')
    unlink (tmp);
  exit (1);
}

static void
msg (const char * fmt, ...)
{
  va_list ap;
  fputs ("[mtcnfdd] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
parse (void)
{
  int i, ch, * clause, lit, sign, size_clause, count_clause, count_clauses;
  FILE * file;
  int zipped;

  if (strlen (src) > 3 && !strcmp (src + strlen (src) - 3, ".gz"))
    {
      const char * gunzip = "gunzip -c %s 2>/dev/null";

      char * cmd = malloc (strlen (src) + strlen (gunzip));
      sprintf (cmd, gunzip, src);
      file = popen (cmd, "r");
      free (cmd);
      zipped = 1;
    }
  else
    {
      file = fopen (src, "r");
      zipped = 0;
    }

  if (!file)
    die ("can not read from '%s'", src);

SKIP:
  ch = getc (file);
  if (isspace (ch))
    goto SKIP;

  if (ch == 'c')
    {
      while ((ch = getc (file)) != '\n' && ch != EOF)
	;
      goto SKIP;
    }

  if (ch != 'p' || fscanf (file, " cnf %d %d", &maxidx, &size_clauses) != 2)
    die ("expected 'p cnf ...' header");

  movedto = malloc ((maxidx + 1) * sizeof (movedto[0]));
  for (i = 1; i <= maxidx; i++)
    movedto[i] = i;

  headclauses = malloc (size_clauses * sizeof (headclauses[0]));

  clause = 0;
  size_clause = count_clause = count_clauses = 0;

NEXT:

  ch = getc (file);
  if (isspace (ch))
    goto NEXT;

  if (ch == 'c')
    {
      while ((ch = getc (file)) != '\n' && ch != EOF)
	;
      goto NEXT;
    }

  if (ch == '-')
    {
      sign = -1;
      ch = getc (file);
      if (ch == EOF)
	die ("EOF after '-'");
    }
  else
    sign = 1;

  if (ch != EOF && !isdigit (ch))
    die ("invalid character %02x", ch);

  if (isdigit (ch))
    {
      lit = ch - '0';
      while (isdigit (ch = getc (file)))
	lit = 10 * lit + (ch - '0');

      lit *= sign;

      if (count_clause == size_clause)
	{
	  size_clause = size_clause ? 2 * size_clause : 8;
	  clause = realloc (clause, size_clause * sizeof (clause[0]));
	}

      clause[count_clause++] = lit;

      if (!lit)
	{
	  if (count_clauses == size_clauses)
	    die ("too many clauses");

	  headclauses[count_clauses++] = clause;
	  count_clause = size_clause = 0;
	  clause = 0;
	}

      goto NEXT;
    }

  assert (ch == EOF);

  if (count_clause)
    die ("missing '0' after clause");

  if (count_clauses < size_clauses)
    die ("%d clauses missing", size_clauses - count_clauses);

  assert (!clause);

  if (zipped)
    pclose (file);
  else
    fclose (file);

  msg ("parsed %d variables", maxidx);
  msg ("parsed %d clauses", size_clauses);
}

static int
maskstatus (int status)
{
  int res = status;
  if (masksignals) res = WEXITSTATUS (res);
  return res;
}

static int
run (const char * name)
{
  char * buffer = malloc (strlen (cmd) + strlen (name) + 100);
  int res;

  __sync_fetch_and_add(&calls, 1);

  /* TODO if this command produces a lot of output, e.g. a solution
   * to a SAT problem, then flushing the associated output buffers of
   * the process generated by 'system' seems to take quiet some time.
   * It is probably better to directly use 'exec' and redirect output
   * in such a way that it does not have to go through the pipe.  Of course
   * users can avoid this effect by not letting the command produce much
   * output through adding appropriate command line options.
   */
  sprintf (buffer, "exec %s %s 1>/dev/null 2>/dev/null", cmd, name);
  res = maskstatus (system (buffer));
  free (buffer);
  return res;
}

static int
deref (int lit)
{
  int idx, res;
  if (!lit)
    return 0;
  idx = abs (lit);
  if (idx == INT_MAX)
    return lit;
  idx = movedto[idx];
  res = (lit < 0) ? -idx : idx;
  return res;
}

static int
clausesatisfied (int ** clauses, int i)
{
  int j, lit;
  if (!clauses[i])
    return 1;
  j = 0;
  while ((lit = clauses[i][j++]))
    if (deref (lit) == TRUE)
      return 1;
  return 0;
}

static int
keptvariables (int ** clauses)
{
  int i, j, idx, lit, res;

  res = 0;
  for (i = 0; i < size_clauses; i++)
    {
      if (clausesatisfied (clauses,i))
	continue;

      j = 0;
      while ((lit = deref (clauses[i][j++])))
	{
	  if (lit == FALSE)
	    continue;

	  assert (lit != TRUE);

	  idx = abs (lit);
	  if (idx > res)
	    res = idx;
	}
    }

  return res;
}

static int
keptclauses (int ** clauses)
{
  int i, res;

  res = 0;
  for (i = 0; i < size_clauses; i++)
    if (!clausesatisfied (clauses,i))
      res++;

  return res;
}

static void
print (int ** clauses, const char * name)
{
  FILE * file = fopen (name, "w");
  int i, j, lit;

  if (!file)
    die ("can not write to '%s'", name);

  fprintf (file, "p cnf %d %d\n", keptvariables (clauses), keptclauses (clauses));

  for (i = 0; i < size_clauses; i++)
    {
      if (clausesatisfied (clauses,i))
	continue;

      j = 0;
      while ((lit = deref (clauses[i][j++])))
	{
	  if (lit == FALSE)
	    continue;

	  fprintf (file, "%d ", lit);
	}

      fputs ("0\n", file);
    }

  fclose (file);
}

int runs_expected(int** clauses, const char* tmp_file)
{
	print (clauses,tmp_file);
	return run (tmp_file) == expected;
}

#define ASSERT_RUNS_EXPECTED(clauses, tmp_file) \
{\
	print (clauses,tmp_file);\
	int run_result = run (tmp_file);\
	if(run_result != expected)\
	{\
		fprintf(stderr,"Result was %d, but expected %d\n",run_result,expected);\
		assert(0);\
	}\
}


static void
setup (int compute_expected)
{
  msg ("copied '%s' to '%s'", src, dst);
  print (headclauses, dst);
  if (compute_expected)
    expected = run (dst);
  msg ("expected exit code %s masking out signals is %d", 
       masksignals ? "after" : "without", expected);
  sprintf (tmp, "/tmp/mtcnfdd-%u", (unsigned) getpid ());
  workerinfo = (WorkerInfo*) calloc(threads,sizeof(WorkerInfo));
  int i;
  for(i=0; i<threads; i++)
  {
	  workerinfo[i].id = i;
	  sprintf (workerinfo[i].tmp, "%s_%d",tmp,i);
  }
  merge_succeeded = 0;
  shrink_succeeded = 0;
  merge_failed = 0;
  shrink_failed = 0;
}

static void
save (void)
{
  print (headclauses,dst);
  changed = 1;
  msg ("saved intermediate result in '%s'", dst);
}

static void
erase (void)
{
  int i;
  fputc ('\r', stderr);
  for (i = 0; i < 79; i++)
    fputc (' ', stderr);
  fputc ('\r', stderr);
}


/**
 * Calculates progress of all threads and prints it to stderr
 */
static void print_reduce_message()
{
	pthread_mutex_lock(&stdout_mutex);
	int sum = 0;
	int removed = 0;
#ifndef HELGRINDSAFE
	int i;
	for(i=0; i<threads; i++)
	{
		sum = sum + workerinfo[i].position - workerinfo[i].start;
		removed += workerinfo[i].removed;
	}
#endif
	fprintf (stderr,
			"[mtcnfdd] reduce(%d) width %d removed %d completed %d/%d          \r",
			round, workerinfo->width, removed, sum, size_clauses);

	fflush (stderr);
	pthread_mutex_unlock(&stdout_mutex);
}

/**
 * Tries to merge the 'failed_test' with the 'headclauses' starting with clauses
 * from thread 'baseid' in steps of 'width'
 */
int try_merge(int** failed_test, int baseid, int max_remove)
{
	int i;
	int removes = workerinfo[baseid].removed;
	int* lastremoved = malloc(max_remove*sizeof(int));

	/* copy result from thread 'baseid' */
	int** test = malloc(size_clauses * sizeof (int*));
	memcpy(test,headclauses,size_clauses * sizeof (int*));
	for(i=workerinfo[baseid].start; i<workerinfo[baseid].end; i++)
	{
		if(test[i] != failed_test[i])
		{
			assert(failed_test[i]==NULL);
			free(headclauses[i]);
			headclauses[i] = NULL;
			test[i] = NULL;
		}
	}
	ASSERT_RUNS_EXPECTED(test,tmp);

	int actual_removes = 0;
	for(i=0; i<size_clauses; i++)
	{
		if(i==workerinfo[baseid].start)
		{
			/* these clauses have already been removed */
			i=workerinfo[baseid].end;
			if(i>=size_clauses) break;
		}
		if(test[i]!=failed_test[i])
		{
			assert(failed_test[i]==NULL);
			lastremoved[actual_removes] = i;
			actual_removes++;
		}
	}
	max_remove = actual_removes;
	int width = max_remove/2;
	//printf("Starting with width %d\n",width);
	int merge_round = 0;
	/* try removing clauses */
	while (width>min_merge_width && merge_round!=max_merge_rounds)
	{
		int start = 0;
		int end;
		assert(max_merge_rounds==-1 || merge_round<max_merge_rounds);
		do
		{
			end = start+width;
			if(end>max_remove) end = max_remove;
			fprintf (stderr,"[mtcnfdd] merge(%d,%d) width %d removed %d completed %d/%d          \r",
					round,merge_round,width, removes,start,max_remove);
			fflush (stderr);
			actual_removes = 0;
			for(i=start; i<end; i++)
			{
				if(test[lastremoved[i]] != failed_test[lastremoved[i]])
				{
					test[lastremoved[i]] = failed_test[lastremoved[i]];
					actual_removes ++;
				}
			}
			//printf("%d were not removed before\n",actual_removes);
			if(actual_removes)
			{
				if(runs_expected(test,tmp))
				{
					/* all clauses could be removed, free them */
					for(i=start;i<end;i++)
					{
						if(test[lastremoved[i]]!=headclauses[lastremoved[i]])
						{
							assert(test[lastremoved[i]]==NULL);
							free(headclauses[lastremoved[i]]);
							headclauses[lastremoved[i]] = NULL;
						}

					}
					removes += actual_removes;
				}
				else
				{
					for(i=start; i<end; i++)
					{
						test[lastremoved[i]] = headclauses[lastremoved[i]];
					}
				}
			}
			start+=width;
		}while(end<max_remove);

		if(width > 1)
			width = (width+1)/2;
		else
			width = 0;
		merge_round++;
	}

	free(lastremoved);
	free(test);
	return removes;
}

/*
 *	Tries to reduce clauses between 'start' and 'end' (parallel)
 */
void* reduce_worker (void* vp)
{
	WorkerInfo* info = (WorkerInfo*) vp;
	int j;
	info->position = info->start;
	int ** test = info->test;
	do
	{
		if (isatty (2))
		{
			print_reduce_message();
		}

		int end = info->position + info->width;
		if (end > info->end)
			end = info->end;

		int found = 0;
		for (j = info->position; j < end; j++)
		{
			if (test[j])
			{
				found++;
				test[j] = NULL;
			}
		}

		if (found)
		{
			if (runs_expected(test,info->tmp))
			{
				info->removed += found;
			}
			else
			{
				for (j = info->position; j < end; j++)
				{
					if (test[j]!=headclauses[j])
					{
						found--;
						test[j] = headclauses[j];
					}
				}
				assert(found==0);
			}
		}
		info->position = end;

	} while (info->position < info->end);
	return NULL;
}

/*
 *	Starts worker to reduce clauses and then tries to merge the reduced clauses
 */
static void reduce (void)
{
	int bytes = size_clauses * sizeof (int*);
	int i, j, width, removed, total;
	if(start_reduce_width==-1) width = size_clauses;
	else
	{
		width = start_reduce_width;
		start_reduce_width = -1;
	}
	total = 0;
	int** test = malloc(bytes);

	/* prepare workers */
	for(i=0; i<threads; i++)
	{
		workerinfo[i].position = 0;
		workerinfo[i].start = 0;
		workerinfo[i].test = malloc (bytes);
		if(!workerinfo[i].test) die("Could not allocate %d bytes of memory",bytes);
	}

	while (width)
	{
		if(!size_clauses) break;

		bytes = size_clauses * sizeof (int*);

		int maxThreads = (size_clauses + width - 1)/width;
		if(threads<maxThreads) maxThreads = threads;
		msg ("reduce(%d) width %d using %d threads", round, width,maxThreads);
		assert(maxThreads>0);

		/* start worker threads */
		int threadWidth = size_clauses/maxThreads;
		for(i=0; i<maxThreads; i++)
		{
			workerinfo[i].removed = 0;
			workerinfo[i].start = i*threadWidth;
			if(i==maxThreads-1)
			{
				workerinfo[i].end = size_clauses;
			}
			else workerinfo[i].end = workerinfo[i].start + threadWidth;
			workerinfo[i].width = width;
			memcpy(workerinfo[i].test,headclauses,bytes);
			pthread_create (&workerinfo[i].thread, NULL, reduce_worker, &workerinfo[i]);
		}

		/* wait for worker threads */
		for(i=0; i<maxThreads; i++)
		{
			pthread_join(workerinfo[i].thread,NULL);
			memcpy(test+workerinfo[i].start
					,workerinfo[i].test+workerinfo[i].start,
					(workerinfo[i].end-workerinfo[i].start) * sizeof(int*));
		}

		/* merge */
		erase ();
		fprintf (stderr,
					"[mtcnfdd] reduce(%d) width %d merging...\r",
					round, workerinfo->width);
		fflush (stderr);

		removed = workerinfo[0].removed;
		int max_removed = workerinfo[0].removed;
		int max_removed_id = 0;
		for(i=1; i<maxThreads; i++)
		{
			if(workerinfo[i].removed)
			{
				removed += workerinfo[i].removed;
				if(workerinfo[i].removed>max_removed)
				{
					max_removed = workerinfo[i].removed;
					max_removed_id = i;
				}
			}
		}

		if(removed)
		{
			if (runs_expected(test,tmp))
			{
				/* all threads results could be merged */
				for(i=0; i<size_clauses; i++)
				{
					if(headclauses[i] != test[i])
					{
						assert(test[i]==NULL);
						free(headclauses[i]);
						headclauses[i] = NULL;
					}
				}
				merge_succeeded++;
			}
			else
			{
				/* pick best result and start to merge from this point*/
				msg ("reduce(%d) width %d removed %d clauses, but merging failed",
								round, width, removed);
				msg ("reduce(%d) thread %d got best result: %d removes",
												round, max_removed_id, max_removed);
				removed = try_merge(test,max_removed_id, removed);
				merge_failed++;
			}
		}

		erase ();
		msg ("reduce(%d) width %d removed %d clauses",
						round, width, removed);

		if (removed)
			save ();

		if (removed && thorough)
			width = size_clauses;
		else if(width > 1)
			width = (width+1)/2;
		else
			width = 0;

		/* reduce 'size_clauses' by moving clauses */
		j = 0;
		for (i = 0; i < size_clauses; i++)
			if (headclauses[i])
				headclauses[j++] = headclauses[i];

		size_clauses = j;
	}

	for(i=0; i<threads; i++)
	{
		free(workerinfo[i].test);
	}
	free(test);
}

/**
 * Calculates progress of all threads and prints it to stderr
 */
static void print_shrink_message()
{
	pthread_mutex_lock(&stdout_mutex);
	int sum = 0;
	int removed = 0;
#ifndef HELGRINDSAFE
	int i;
	for(i=0; i<threads; i++)
	{
		sum = sum + workerinfo[i].position - workerinfo[i].start;
		removed += workerinfo[i].removed;
	}
#endif
	fprintf (stderr,
			"[mtcnfdd] shrink(%d) removed %d completed %d/%d          \r",
			round, removed , sum, size_clauses);

	fflush (stderr);
	pthread_mutex_unlock(&stdout_mutex);
}

/*
 * Tries to reduce variables in clauses, when one clause is finished it synchronizes with
 * headclauses
 */
void* shrink_worker (void* vp)
{
	WorkerInfo* info = (WorkerInfo*) vp;
	int j, lit;
	int* tmpClause = malloc((maxidx+1) * sizeof(int));
	int** test = info->test;
	int* original_clause;

	for (info->position = info->start; info->position < info->end; info->position++)
	{
		if (!test[info->position])
			continue;
		original_clause = test[info->position];
		/* save variables */
		for (j = 0; (lit = original_clause[j]); j++)
		{
			tmpClause[j] = original_clause[j];
		}
		tmpClause[j] = original_clause[j];
		assert(!tmpClause[j]);
		int removed = 0;
		test[info->position] = tmpClause;
		/* test local remove */
		for (j = 0; (lit = tmpClause[j]); j++)
		{
			if (lit == FALSE)
				continue;

			tmpClause[j] = FALSE;
			if (runs_expected(test,info->tmp))
				removed++;
			else
				tmpClause[j] = lit;
		}

		/* synchronize */
		if(removed)
		{
			pthread_mutex_lock(&headclauses_mutex);
			for (j = 0; (lit = tmpClause[j]); j++)
			{
				if(original_clause[j] != lit)
				{
					tmpClause[j] = original_clause[j];
					original_clause[j] = lit;
				}
			}
			if (runs_expected(headclauses,info->tmp))
			{
				info->removed+=removed;
				shrink_succeeded++;
			}
			else
			{
				for (j = 0; (lit = tmpClause[j]); j++)
				{
					if(original_clause[j] != lit)
					{
						original_clause[j] = lit;
					}
				}
				shrink_failed++;
			}
			pthread_mutex_unlock(&headclauses_mutex);
		}
		test[info->position] = original_clause;
		print_shrink_message();
	}
	free(tmpClause);
	return NULL;
}

/**
 * Starts workers to remove variables from clauses
 */
static void shrink (void)
{
	int i, removed;
	int bytes = size_clauses * sizeof (int*);

	if(!size_clauses) return;

	/* prepare workers*/
	for(i=0; i<threads; i++)
	{
		workerinfo[i].position = 0;
		workerinfo[i].start = 0;
		workerinfo[i].test = malloc (bytes);
		if(!workerinfo[i].test) die("Could not allocate %d bytes of memory",bytes);
	}

	/* start workers */
	int maxThreads = size_clauses;
	if(threads<maxThreads) maxThreads = threads;
	assert(maxThreads>0);
	int threadWidth = size_clauses/maxThreads;
	for(i=0; i<maxThreads; i++)
	{
		workerinfo[i].removed = 0;
		workerinfo[i].start = i*threadWidth;
		if(i==maxThreads-1)
		{
			workerinfo[i].end = size_clauses;
		}
		else workerinfo[i].end = workerinfo[i].start + threadWidth;
		memcpy(workerinfo[i].test,headclauses,bytes);
		pthread_create (&workerinfo[i].thread, NULL, shrink_worker, &workerinfo[i]);
	}

	/* wait for workers */
	removed = 0;
	for(i=0; i<maxThreads; i++)
	{
		pthread_join(workerinfo[i].thread,NULL);
		removed += workerinfo[i].removed;
	}
	for(i=0; i<threads; i++)
	{
		free(workerinfo[i].test);
	}



	erase ();

	msg ("shrink(%d) removed %d literals", round, removed);

	if (removed)
		save ();
}

static void
move (void)
{
  char * used = malloc (maxidx + 1);
  int i, j, idx, count, * saved, movedtomaxidx, moved;

  for (i = 1; i <= maxidx; i++)
    used[i] = 0;

  for (i = 0; i < size_clauses; i++)
    {
      if (!headclauses[i])
	continue;

      j = 0;
      while ((idx = abs (headclauses[i][j++])))
	if (idx != INT_MAX)
	  used[idx] = 1;
    }

  movedtomaxidx = 0;
  count = 0;
  for (i = 1; i <= maxidx; i++)
    {
      if (!used[i])
	continue;

      if (movedto[i] > movedtomaxidx)
	movedtomaxidx = movedto[i];

      count++;
    }

  moved = movedtomaxidx - count;
  if (count && moved)
    {
      saved = malloc ((maxidx + 1) * sizeof (saved[0]));
      for (i = 1; i <= maxidx; i++)
	saved[i] = movedto[i];

      j = 0;
      for (i = 1; i <= maxidx; i++)
	if (used[i])
	  movedto[i] = ++j;

      assert (j == count);

      if (!runs_expected(headclauses, tmp))
	{
	  moved = 0;
	  for (i = 1; i <= maxidx; i++)
	    movedto[i] = saved[i];
	}
      else
	assert (run (dst) == expected);

      free (saved);
    }

  free (used);

  if (moved)
    {
      msg ("removed %d variables", moved);
      save ();
    }
}

static void
reset (void)
{
  int i;
  for (i = 0; i < size_clauses; i++)
    free (headclauses[i]);
  free (headclauses);
  free (movedto);
  free (cmd);
  for(i=0; i<threads; i++)
  {
	  unlink(workerinfo[i].tmp);
  }
  free (workerinfo);
  unlink (tmp);
}

int
main (int argc, char ** argv)
{
	int i;
        int compute_expected = 1;

	threads = 1;
	min_merge_width = 0;
	max_merge_rounds = -1;
	start_reduce_width = -1;
	for (i = 1; i < argc; i++)
	{
		if (!cmd && !strcmp (argv[i], "-h"))
		{
			printf ("%s", USAGE);
			exit (0);
		}
		else if (!cmd && !strcmp (argv[i], "-t"))
                          thorough = 1;
		else if (!cmd && !strcmp (argv[i], "-m"))
		  	  masksignals = 1;
                else if (!cmd && !strcmp (argv[i], "-e"))
                  {
                    if (i == argc - 1)
                      die ("expected exit code missing");
                    i++;
                    expected = atoi (argv[i]);
                    compute_expected = 0;
                  }
		else if (!cmd && !strcmp (argv[i], "-version"))
		{
			printf("Version: %d\n",VERSION_NUMBER);
			return 0;
		}
		else if (!cmd && !strcmp (argv[i], "-T"))
		{
			i++;
			if(i==argc) die("number of threads not specified");
			else threads = atoi(argv[i]);
		}
		else if (!cmd && !strcmp (argv[i], "-s"))
		{
			i++;
			if(i==argc) die("start reduce width not specified");
			else start_reduce_width = atoi(argv[i]);
		}
		else if (!cmd && !strcmp (argv[i], "-mmw"))
		{
			i++;
			if(i==argc) die("minimum merge width not specified");
			else min_merge_width = atoi(argv[i]);
		}
		else if (!cmd && !strcmp (argv[i], "-mmr"))
		{
			i++;
			if(i==argc) die("maximum merge round not specified");
			else max_merge_rounds = atoi(argv[i]);
		}
		else if (!cmd && argv[i][0] == '-')
			die ("invalid command line option '%s'", argv[i]);
		else if (cmd)
		{
			char * old = cmd;
			cmd = malloc (strlen (old) + 1 + strlen (argv[i]) + 1);
			sprintf (cmd, "%s %s", old, argv[i]);
			free (old);
		}
		else if (dst)
			cmd = strdup (argv[i]);
		else if (src)
			dst = argv[i];
		else
			src = argv[i];
	}

	if(threads<1)
			die ("bad threads parameter");

	if(min_merge_width<0)
			die ("bad minimum merge width parameter");
	if(max_merge_rounds<-1)
				die ("bad minimum merge width parameter");

	if (!src)
		die ("'src' missing");

	if (!dst)
		die ("'dst' missing");

	if (!cmd)
		die ("'cmd' missing");

	parse ();
	setup (compute_expected);
	msg("using %d thread(s)", threads);
	msg("minimum merge width: %d", min_merge_width);
	msg("maximum merge rounds: %d (-1 = infinite)", max_merge_rounds);

	changed = 1;
	for (round = 1; changed; round++)
	{
		changed = 0;
		reduce ();
		move ();
		shrink ();
		move ();
		ASSERT_RUNS_EXPECTED(headclauses,tmp);
	}

	msg ("called '%s' %d times", cmd, calls);
	msg ("merge failed %d/%d times", merge_failed,merge_failed+merge_succeeded);
	msg ("shrink failed %d/%d times", shrink_failed,shrink_failed+shrink_succeeded);
	msg ("kept %d variables", keptvariables (headclauses));
	msg ("kept %d clauses", keptclauses (headclauses));

	reset ();

	return 0;
}
