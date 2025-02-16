/* Copyright (c) 2006 - 2011, Armin Biere, Johannes Kepler University. */

#define USAGE \
  "usage: cnfdd [-h|-t] src dst cmd [<cmdopt> ...]\n" \
  "\n" \
  "  -h     print this command line option summary\n" \
  "  -t     thorough mode, e.g. iterate same widths multiple times\n" \
  "  -m     mask out signals from exit code\n" \
  "  -c     compute clausal core (do not shrink)\n" \
  "  -q     quantify explicitly even outer-most existential variables\n" \
  "  -r     remove options\n" \
  "  -e <e> set expected exit code to <e>\n" \
  "\n" \
  "  src    file name of an existing CNF in DIMACS or QDIMACS format\n" \
  "  dst    file name of generated minimized CNF\n" \
  "  cmd    command to debug (expects a CNF file as argument)\n" \
  "\n" \
  "The delta debugger copies 'src' to 'dst' and tries to remove\n" \
  "as many clauses and literals without changing the exit code\n" \
  "of 'cmd dst'.  Then unused variables are removed, as long the\n" \
  "exit code does not change.\n" \
  "\n" \
  "Comments before the header are scanned for option value pairs\n" \
  "of the form '--<option-name>=<integer-value>'.  These are kept\n" \
  "in the reduced and intermediate files as well and delta\n" \
  "debugged towards 0.\n"

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

#define TRUE INT_MAX
#define FALSE -INT_MAX

static const char * src;
static const char * dst;
static char * cmd;

static int *qbf;
static int *prefix;
static int quantified;
static int quantify;
static int ** clauses;
static int size_clauses;
static int maxidx;
static char * used;
static int * movedto;
static int expected;
static char tmp[100];
static int round;
static int changed;
static int calls;
static int thorough;
static int masksignals;
static int removeopts;
static char ** options;
static int * values;
static int szopts;
static int nopts;
static int coreonly;

static void
die (const char * fmt, ...)
{
  va_list ap;
  fputs ("*** cnfdd: ", stderr);
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
  fputs ("[cnfdd] ", stderr);
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
  int zipped, val, szbuf = 0, nbuf = 0, quantifier = 0;
  char * buffer = 0;
  FILE * file;
 
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
      ch = getc (file);
      while (ch != '\n' && ch != EOF) 
	{
	  if (ch != '-') 
	    {
	      ch = getc (file);
	      continue;
	    }
	  ch = getc (file);
	  if (ch == '-') 
	    {
	      nbuf = 0;
	      ch = getc (file);
	      while (isalnum (ch) || ch == '-' || ch == '_') 
		{
		  if (nbuf + 1 >= szbuf)
		    buffer = realloc (buffer, szbuf = szbuf ? 2*szbuf : 2);
		  buffer[nbuf++] = ch;
		  buffer[nbuf] = 0;
		  ch = getc (file);
		}
	      if (ch == '=') 
		{
		  ch = getc (file);
		  if (ch == '-') 
		    { 
		      sign = -1;
		      ch = getc (file);
		    }
		  else
		     sign = 1;
		  if (isdigit (ch)) 
		    {
		      val = ch - '0';
		      ch = getc (file);
		      while (isdigit (ch)) 
			{
			  val = 10 * val + (ch - '0');
			  ch = getc (file);
			}

		      val *= sign;
		      msg ("parsed embedded option: --%s=%d", buffer, val);

		      if (nopts >= szopts) 
			{
			  szopts = szopts ? 2 * szopts : 1;
			  options =
			    realloc (options, szopts * sizeof *options);
			  values = realloc (values, szopts * sizeof *values);
			}
		      options[nopts] = strdup (buffer);
		      values[nopts] = val;
		      nopts++;
		    }
		}
	    }
	}
      goto SKIP;
    }

  free (buffer);

  if (ch != 'p' || fscanf (file, " cnf %d %d", &maxidx, &size_clauses) != 2)
    die ("expected 'p cnf ...' header");

  movedto = malloc ((maxidx + 1) * sizeof *movedto);
  for (i = 1; i <= maxidx; i++)
    movedto[i] = i;

  used = calloc (maxidx + 1, sizeof * used);
  for (i = 1; i <= maxidx; i++)
    used[i] = 1;

  clauses = malloc (size_clauses * sizeof *clauses);

  clause = 0;
  size_clause = count_clause = count_clauses = 0;

NEXT:

  ch = getc (file);
  if (isspace (ch))
    goto NEXT;

  if (ch == 'a' || ch == 'e')
    {
      if (count_clauses)
	die ("quantifier after clause");

      if (quantifier)
	die ("'0' after quantifier missing");

      if (!qbf)
	{
	  assert (!prefix);
	  qbf = calloc (maxidx + 1, sizeof *qbf);
	  prefix = malloc (maxidx * sizeof *prefix);
	}
      else
	assert (qbf && prefix);

      if (ch == 'e')
	quantifier = 1;

      if (ch == 'a')
	quantifier = -1;

      goto NEXT;
    }

  if (ch == 'c')
    {
      while ((ch = getc (file)) != '\n' && ch != EOF)
	;
      goto NEXT;
    }

  if (ch == '-')
    {
      if (quantifier) 
	die ("'-' in quantifier declaration");

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

      if (!quantifier)
	{
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

	      clauses[count_clauses++] = clause;
	      count_clause = size_clause = 0;
	      clause = 0;
	    }
	}
      else if (lit)
	{
	  assert (sign == 1);
	  if (qbf[lit])
	    die ("variable %d quantified twice", lit);

	  prefix[quantified++] = quantifier * lit;
	  qbf[lit] = quantifier * abs (quantified);
	}
      else
	quantifier = 0;

      goto NEXT;
    }

  assert (ch == EOF);

  if (count_clause)
    die ("missing '0' after clause");

  if (quantifier)
    die ("missing '0' after quantifier declaration");

  if (count_clauses < size_clauses)
    die ("%d clauses missing", size_clauses - count_clauses);

  assert (!clause);

  if (zipped)
    pclose (file);
  else
    fclose (file);

  msg ("parsed %d variables", maxidx);
  msg ("parsed %d clauses", size_clauses);
  if (qbf) msg ("this is actually a QBF instance");
  else msg ("this is a propositional instance");
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

  calls++;

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
clausesatisfied (int i)
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
keptvariables (void)
{
  int i, res;

  res = 0;
  for (i = 1; i <= maxidx; i++)
    if (used[i] && movedto[i] > res)
      res = movedto[i];

  return res;
}

static int
keptclauses (void)
{
  int i, res;

  res = 0;
  for (i = 0; i < size_clauses; i++)
    if (!clausesatisfied (i))
      res++;

  return res;
}

static int
sgn (int lit) 
{
  if (!lit) return 0;
  return lit < 0 ? -1 : 1;
}

static void
print (const char * name)
{
  FILE * file = fopen (name, "w");
  int i, j, lit, quantifier = 0;

  if (!file)
    die ("can not write to '%s'", name);

  for (i = 0; i < nopts; i++)
    if (options[i])
      fprintf (file, "c --%s=%d\n", options[i], values[i]);

  fprintf (file, "p cnf %d %d\n", keptvariables (), keptclauses ());

  if (qbf)
    {
      quantifier = 0;
      if (quantify)
	for (i = 1; i <= maxidx; i++) 
	  {
	    if (!used[i])
	      continue;

	    if (qbf[i])
	      continue;

	    if (!quantifier)
	      {
		quantifier = -1;
		fprintf (file, "e");
	      }

	    fprintf (file, " %d", i);
	  }

      for (i = 0; i < quantified; i++) 
	{
	  lit = prefix[i];

	  if (!used[ abs (lit)])
	    continue;

	  if (sgn (lit) != quantifier)
	    {
	      if (quantifier)
		fprintf (file, " 0\n");

	      if (lit < 0)
		{
		  quantifier = -1;
		  fprintf (file, "a");
		}
	      else
		{
		  quantifier = 1;
		  fprintf (file, "e");
		}
	    }
	  fprintf (file, " %d", deref (abs (lit)));
	}

      if (quantifier)
	fprintf (file, " 0\n");
    }

  for (i = 0; i < size_clauses; i++)
    {
      if (clausesatisfied (i))
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

static void
setup (int compute_expected)
{
  msg ("copied '%s' to '%s'", src, dst);
  print (dst);
  if (compute_expected)
    expected = run (dst);
  msg ("expected exit code %s masking out signals is %d",
       masksignals ? "after" : "without", expected);
  sprintf (tmp, "/tmp/cnfdd-%u", (unsigned) getpid ());
}

static void
save (void)
{
  print (dst);
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

static void
reduce (void)
{
  int ** saved = malloc (size_clauses * sizeof *saved);
  int i, j, end, width, found, removed, total;

  width = size_clauses;
  total = 0;

  while (width)
    {
      if (!isatty (2))
	msg ("reduce(%d) width %d", round, width);

      removed = 0;
      i = 0;

      do {

	if (isatty (2))
	  {
	    fprintf (stderr,
	      "[cnfdd] reduce(%d) width %d removed %d completed %d/%d\r",
	      round, width, removed, i, size_clauses);

	    fflush (stderr);
	  }

	end = i + width;
	if (end > size_clauses)
	  end = size_clauses;

	found = 0;
	for (j = i; j < end; j++)
	  {
	    if (clauses[j])
	      {
		found++;
		saved[j] = clauses[j];
		clauses[j] = 0;
	      }
	    else
	      saved[j] = 0;
	  }

	if (found)
	  {
	    print (tmp);
	    if (run (tmp) == expected)
	      {
		for (j = i; j < end; j++)
		  {
		    if (saved[j])
		      {
			total++;
			removed++;
			free (saved[j]);
		      }
		  }
	      }
	    else
	      {
		for (j = i; j < end; j++)
		  clauses[j] = saved[j];
	      }
	  }

	i = end;

      } while (i < size_clauses);

      if (isatty (2))
	erase ();

      msg ("reduce(%d) width %d removed %d clauses",
	   round, width, removed);

      if (removed)
	save ();

      if (removed && thorough)
	width = size_clauses;
      else if (width > 1)
	width = (width+1)/2;
      else width = 0;

      j = 0;
      for (i = 0; i < size_clauses; i++)
	if (clauses[i])
	  clauses[j++] = clauses[i];

      size_clauses = j;
    }

  free (saved);
}

static void
shrink (void)
{
  int i, j, lit, removed;

  removed = 0;
  for (i = 0; i < size_clauses; i++)
    {
      if (!clauses[i])
	continue;

      for (j = 0; (lit = clauses[i][j]); j++)
	{
	  if (lit == FALSE)
	    continue;

	  clauses[i][j] = FALSE;
	  print (tmp);
	  if (run (tmp) == expected)
	    removed++;
	  else
	    clauses[i][j] = lit;
	}

      if (isatty (2))
	{
	  fprintf (stderr,
		   "[cnfdd] shrink(%d) removed %d completed %d/%d\r",
		   round, removed, i, size_clauses);

	  fflush (stderr);
	}
    }

  if (isatty (2))
    erase ();

  msg ("shrink(%d) removed %d literals", round, removed);

  if (removed)
    save ();
}

static void
move (void)
{
  char * occur = calloc (maxidx + 1, sizeof *occur), * sused;
  int i, j, count, * saved, movedtomaxidx, moved, idx;

  for (i = 0; i < size_clauses; i++)
    {
      if (!clauses[i])
	continue;

      j = 0;
      while ((idx = abs (clauses[i][j++])))
	if (idx != INT_MAX)
	  occur[idx] = 1;
    }

  movedtomaxidx = 0;
  count = 0;
  for (i = 1; i <= maxidx; i++)
    {
      if (!occur[i])
	continue;

      if (movedto[i] > movedtomaxidx)
	movedtomaxidx = movedto[i];

      count++;
    }

  moved = movedtomaxidx - count;
  if (count && moved)
    {
      saved = malloc ((maxidx + 1) * sizeof *saved);
      for (i = 1; i <= maxidx; i++)
	saved[i] = movedto[i];

      j = 0;
      for (i = 1; i <= maxidx; i++)
	if (occur[i])
	  movedto[i] = ++j;

      assert (j == count);

      sused = used;
      used = occur;
      print (tmp);
      if (run (tmp) != expected)
	{
	  moved = 0;
	  for (i = 1; i <= maxidx; i++)
	    movedto[i] = saved[i];
	  used = sused;
	}
      else
	occur = sused;

      free (saved);
    }

  free (occur);

  if (moved)
    {
      msg ("removed %d variables", moved);
      save ();
    }

  assert (run (dst) == expected);

}

static void
opts (void)
{
  int i, val, removed, reduced, reductions, once, c, n, shift, delta;
  char * opt;

  n = 0;
  for (i = 0; i < nopts; i++) 
    if (options[i])
      n++;

  removed = 0;
  if (removeopts)
    {
      c = 0;
      for (i = 0; i < nopts; i++) 
	{
	  if (!options[i])
	    continue;

	  c++;

	  if (isatty (2))
	    {
	      fprintf (stderr,
		       "[cnfdd] opts(%d) removed %d completed %d/%d\r",
		       round, removed, c, n);

	      fflush (stderr);
	    }

	  opt = options[i];
	  options[i] = 0;
	  print (tmp);
	  if (run (tmp) == expected) 
	    {
	      removed++;
	      free (opt);
	    }
	  else
	    options[i] = opt;
	}

      if (isatty (2))
	erase ();

      if (removed) 
	{
	  msg ("opts(%d) removed %d options", round, removed);
	  save ();
	}
    }

  c = 0;
  n -= removed;
  reductions = reduced = 0;

  for (i = 0; i < nopts; i++) 
    {
      if (!options[i])
	continue;

      if (isatty (2))
	{
	  fprintf (stderr,
		   "[cnfdd] opts(%d) "
		   "reduced %d completed %d/%d in %d reductions\r",
		   round, reduced, c, n, reductions);

	  fflush (stderr);
	}

      shift = 1;
      once = 0;
      for (;;)
	{
	  val = values[i];
	  delta = val / (1 << shift);
	  if (!delta) break;
	  assert (abs (delta) < abs (val));
	  values[i] -= delta;
	  print (tmp);
	  if (run (tmp) != expected)
	    {
	      values[i] = val;
	      shift++;
	    }
	  else
	    {
	      once = 1;
	      reductions++;
	    }
	}

      for (;;)
	{
	  val = values[i];
	  if (val > 0) values[i]--;
	  else if (val < 0) values[i]++;
	  else break;
	  print (tmp);
	  if (run (tmp) != expected)
	    {
	      values[i] = val;
	      break;
	    }
	  else
	    {
	      once = 1;
	      reductions++;
	    }
	}

      if (isatty (2))
	erase ();

      if (once) 
	reduced++;
    }

  if (isatty (2))
    erase ();

  if (reduced)
    save ();

  if (reduced)
    msg ("opts(%d) reduced %d option values in %d reductions",
         round,  reduced, reductions);
}

static void
reset (void)
{
  int i;
  for (i = 0; i < size_clauses; i++)
    free (clauses[i]);
  free (clauses);
  free (movedto);
  free (used);
  free (qbf);
  free (prefix);
  free (cmd);
  for (i = 0; i < nopts; i++)
    free (options[i]);
  free (options);
  unlink (tmp);
}

int
main (int argc, char ** argv)
{
  int i;
  int compute_expected = 1;

  for (i = 1; i < argc; i++)
    {
      if (!cmd && !strcmp (argv[i], "-h"))
	{
	  printf ("%s", USAGE);
	  exit (0);
	}
      else if (!cmd && !strcmp (argv[i], "-t"))
	thorough = 1;
      else if (!cmd && !strcmp (argv[i], "-r"))
	removeopts = 1;
      else if (!strcmp (argv[i], "-m"))
	masksignals = 1;
      else if (!strcmp (argv[i], "-q"))
	quantify = 1;
      else if (!strcmp (argv[i], "-c"))
	coreonly = 1;
      else if (!cmd && !strcmp (argv[i], "-e"))
        {
          if (i == argc - 1)
            die ("expected exit code missing");
          i++;
          expected = atoi (argv[i]);
          compute_expected = 0;
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

  if (!src)
    die ("'src' missing");

  if (!dst)
    die ("'dst' missing");

  if (!cmd)
    die ("'cmd' missing");

  parse ();
  setup (compute_expected);

  changed = 1;
  for (round = 1; changed; round++)
    {
      changed = 0;
      reduce ();
      if (coreonly) continue;
      move ();
      shrink ();
      move ();
      opts ();
    }

  msg ("called '%s' %d times", cmd, calls);

  msg ("kept %d variables", keptvariables ());
  msg ("kept %d clauses", keptclauses ());
  reset ();

  return 0;
}
