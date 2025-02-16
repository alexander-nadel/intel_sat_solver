/* Copyright (c) 2009 - 2010, Armin Biere, Johannes Kepler University. */
/* Copyright (c) 2024 - 2025, Alexander Nadel, Technion. */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/times.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <vector>

using namespace std;

#define MAX 20
static int clause[MAX + 1];

static int
pick (int from, int to)
{
  assert (from <= to);
  return (rand() % (to - from + 1)) + from;
}

static int
numstr (const char * str)
{
  const char * p;
  for (p = str; *p; p++)
    if (!isdigit (*p))
      return 0;
  return 1;
}

#define SIGN() ((pick (31,32) == 32) ? -1 : 1)

int
main (int argc, char ** argv)
{
  int i, j, k, l, m, n, o, p, sign, lit, layer, w, val, min, max, ospread;
  int ** unused, * nunused, allmin, allmax, qbf, *quant, scramble, * map;
  int seed, nlayers, ** layers, *width, * low, * high, * clauses;
  int fp, eqs, ands, *arity, maxarity, lhs, rhs;
  const char * options;
  char option[100];
  FILE * file;
  char * mark;

  qbf = 0;
  seed = -1;
  options = 0;

  for (i = 1; i < argc; i++) 
    {
      if (!strcmp (argv[i], "-h")) 
	{
	  printf (
"usage: cnfuzz_incr [-h][-q][<seed>][<option-file>]\n"
"\n"
"  -h   print command line option help\n"
"  -q   generate quantified CNF in QDIMACS format\n"
"\n"
"If the seed is not specified it is calculated from the process id\n"
"and the current system time (in seconds).\n"
"\n"
"The optional <option-file> lists integer options with their ranges,\n"
"one option in the format '<opt> <lower> <upper> per line.\n"
"Those options are fuzzed and embedded into the generated input\n"
"in comments before the 'p cnf ...' header.\n"
);
	  exit (0);
	}
      if (!strcmp (argv[i], "-q")) 
	qbf = 1;
      else if (numstr (argv[i])) 
	{
	  if (seed >= 0) 
	    {
	      fprintf (stderr, "*** cnfuzz: multiple seeds\n");
	      exit (1);
	    }
	  seed = atoi (argv[i]);
	  if (seed < 0) 
	    {
	      fprintf (stderr, "*** cnfuzz: seed overflow\n");
	      exit (1);
	    }
	}
      else if (options) 
	{
	  fprintf (stderr, "*** cnfuzz: multiple option files\n");
	  exit (1);
	}
      else
	options = argv[i];
    }

  if (seed < 0) 
  {
	const auto t0 = times(0);
	const auto pid = getpid();
	const auto rs = (t0 * pid) >> 1;
	const auto newseed = abs(rs);
	seed = newseed;
	printf ("c Fixing a negative seed: t0 = %d; pid = %d; rs = %d; newseed = %d; seed = %d\n", t0, pid, rs, newseed, seed);	
  }

  srand (seed);
  printf ("c seed %d\n", seed);
  fflush (stdout);
  if (qbf) 
    {
      printf ("c qbf\n");
      fp = pick (0, 3);
      if (fp)
	printf ("c but forced to be propositional\n");
    }
  if (options)
    {
      file = fopen (options, "r");
      ospread = pick (0, 10);
      if ((allmin = pick (0, 1)))
	printf ("c allmin\n");
      else if ((allmax = pick (0, 1)))
	printf ("c allmax\n");
      printf ("c %d ospread\n", ospread);
      if (!file)
	{
	  fprintf (stderr, "*** cnfuzz: can not read '%s'\n", options);
	  exit (1);
	}
      while (fscanf (file, "%s %d %d %d", option, &val, &min, &max) == 4)
	{
	  if (!pick (0, ospread)) 
	    {
	      if (allmin) val = min;
	      else if (allmax) val = max;
	      else val = pick (min, max);
	    }
	  printf ("c --%s=%d\n", option, val);
	}
      fclose (file);
    }
  srand (seed);
  w = pick (10, 70);
  printf ("c width %d\n", w);
  scramble = pick (-1, 1);			/* TODO finish */
  printf ("c scramble %d\n", scramble);
  nlayers = pick (1, 20);
  printf ("c layers %d\n", nlayers);
  eqs = pick (0, 2) ? 0 : pick (0, 99);
  printf ("c equalities %d\n", eqs);
  ands = pick (0, 1) ? 0 : pick (0,99);
  printf ("c ands %d\n", ands);
  
  layers = (int**)calloc (nlayers, sizeof *layers);
  quant = (int*)calloc (nlayers, sizeof *quant);
  width = (int*)calloc (nlayers, sizeof *width);
  low = (int*)calloc (nlayers, sizeof *low);
  high = (int*)calloc (nlayers, sizeof *high);
  clauses = (int*)calloc (nlayers, sizeof *clauses);
  unused = (int**)calloc (nlayers, sizeof *unused);
  nunused = (int*)calloc (nlayers, sizeof *nunused);
  for (i = 0; i < nlayers; i++)
    {
      width[i] = pick (10, w);
      quant[i] = (qbf && !fp) ? pick (-1, 1) : 0;
      low[i] = i ? high[i-1] + 1 : 1;
      high[i] = low[i] + width[i] - 1;
      m = width[i];
      if (i) m += width[i-1];
      n = (pick (300, 450) * m) / 100;
      clauses[i] = n;
      printf ("c layer[%d] = [%d..%d] w=%d v=%d c=%d r=%.2f q=%d\n",
              i, low[i], high[i], width[i], m, n, n / (double) m, quant[i]);

      nunused[i] = 2 * (high[i] - low[i] + 1);
      unused[i] = (int*)calloc (nunused[i], sizeof *unused[i]);
      k = 0;
      for (j = low[i]; j <= high[i]; j++)
	for (sign = -1; sign <= 1; sign += 2)
	  unused[i][k++] = sign * j;
      assert (k == nunused[i]);
    }
  arity = (int*)calloc (ands, sizeof *arity);
  maxarity = m/2;
  if (maxarity >= MAX)
    maxarity = MAX - 1;
  for (i = 0; i < ands; i++)
    arity[i] = pick (2, maxarity);
  n = 0;
  for (i = 0; i < ands; i++)
    n += arity[i] + 1;
  m = high[nlayers-1];
  mark = (char*)calloc (m + 1, 1);
  for (i = 0; i < nlayers; i++)
    n += clauses[i];
  n += 2*eqs;
  
  // Incremental
  
  // No header for incremental 
  // m: variables
  // n: clauses
  printf ("c pcnf-line-variables-clauses %d %d\n", m, n);
  
  int clssSoFar = 0;
  
  const int maxAssumps = pick(1, m);
  printf ("c maxAssumps %d\n", maxAssumps);
  
  int incBlockEveryNClss = pick(0, n);
   auto GetIncrBlocks = [&]()
   {
	   return incBlockEveryNClss == 0 ? 0 : n / incBlockEveryNClss;
   };
  int maxQueriesPerBlock = pick(1, 100);
  
  auto GetAverage = [](int q)
  {
	  return (1+q)/2;
  };
  auto GetExpectedQueries = [&]()
	{
		return GetAverage(GetIncrBlocks()) * GetAverage(maxQueriesPerBlock);
	};
	
	
  // incBlockEveryNClss == 0 --> no incremental queries at all
  const int expectedQueriesThreshold = 1000;
  // For the rest, make sure no more than expectedQueriesThreshold queries on average
  if (incBlockEveryNClss != 0)
  {
	  
	if (GetExpectedQueries() > expectedQueriesThreshold)
	{
		printf ("c expectedQueries %d > %d --> increasing incBlockEveryNClss %d (GetIncrBlocks = %d) and/or decreasing maxQueriesPerBlock %d\n", GetExpectedQueries(), expectedQueriesThreshold, incBlockEveryNClss, GetIncrBlocks(), maxQueriesPerBlock);
	}
	
	for (int expectedQueries = GetExpectedQueries(); expectedQueries > expectedQueriesThreshold; expectedQueries = GetExpectedQueries())
	{		
		if (incBlockEveryNClss >= n)
		{
			assert(maxQueriesPerBlock > 1);
			--maxQueriesPerBlock;
		} else if (maxQueriesPerBlock <= 1)
		{
			assert(incBlockEveryNClss <= n);
			++incBlockEveryNClss;
		}
		else
		{
			if (pick(0, 1))
			{
				++incBlockEveryNClss;
			}
			else
			{
				--maxQueriesPerBlock;			
			}
		}
	}
  }
  
  printf ("c expectedQueries %d < %d; incBlockEveryNClss %d ; incrBlocks %d ; maxQueriesPerBlock %d\n", GetExpectedQueries(), expectedQueriesThreshold, incBlockEveryNClss, GetIncrBlocks(), maxQueriesPerBlock);
  
  auto PrintQuery = [&](const vector<int>& assumps)
  {
	  printf("s ");
	  for (auto l : assumps) 
	  {
		printf("%d ", l);
	  }
	  printf("0\n");
  };
  
  auto NewClause = [&]()
  {
	  ++clssSoFar;
	  if (incBlockEveryNClss != 0 && (clssSoFar % incBlockEveryNClss) == 0)
	  {
		  const auto maxAssumpsForThisBlock = pick(1, maxAssumps);
		  vector<int> assumps;
		  assumps.reserve(maxAssumpsForThisBlock);
		  while (assumps.size() < maxAssumpsForThisBlock)
		  {
			  assumps.emplace_back(pick(1,m));
			  if (pick(0,1))
			  {
				  assumps.back() = -assumps.back();
			  }
		  }
		  
		  const auto queriesInBlock = pick(1, maxQueriesPerBlock);
		  
		  for (int currQuery = 1; currQuery <= queriesInBlock; ++currQuery)
		  {
			  vector<int> currAssumps;
			  for (auto a : assumps)
			  {
				  if (pick(0,1))
				  {
					if (pick(0, 9))
					{
						currAssumps.emplace_back(a);
					}
					else
					{
						currAssumps.emplace_back(-a);
					}
				  }
			  }
			  PrintQuery(currAssumps);
		  }
		  
	  }
  };
  
  
  map = (int*)calloc (2*m + 1, sizeof *map);
  map += m;
  if (qbf && !fp) 
    for (i = 0; i < nlayers; i++)
      {
	if (!i && !quant[0]) continue;
	fputc (quant[i] < 0 ? 'a' : 'e', stdout);
	for (j = low[i]; j <= high[i]; j++)
	  printf (" %d", j);
	fputs (" 0\n", stdout);
      }
  for (i = 0; i < nlayers; i++)
    {
      for (j = 0; j < clauses[i]; j++)
	{
	  l = 3;
	  while (l < MAX && pick (17, 19) != 17)
	    l++;

	  for (k = 0; k < l; k++)
	    {
	      layer = i;
	      while (layer && pick (3, 4) == 3)
		layer--;
	      if (nunused[layer])
		{
		  o = nunused[layer] - 1;
		  p = pick (0, o);
		  lit = unused[layer][p];
		  if (mark [abs (lit)]) continue;
		  nunused[layer] = o;
		  if (p != o) unused[layer][p] = unused[layer][o];
		}
	      else
		{
		  lit = pick (low[layer], high[layer]);
		  if (mark[lit]) continue;
		  lit *= SIGN ();
		}
	      clause[k] = lit;
	      mark[abs (lit)] = 1;
	      printf ("%d ", lit);
	    }
	  printf ("0\n");
	  NewClause();
	  for (k = 0; k < l; k++)
	    mark[abs (clause[k])] = 0;
	}
    }
  while (eqs-- > 0)
    {
      i = pick (0, nlayers - 1);
      j = pick (0, nlayers - 1);
      k = pick (low[i], high[i]);
      l = pick (low[j], high[j]);
      if (k == l)
	{
	  eqs++;
	  continue;
	}
      k *= SIGN ();
      l *= SIGN ();
      printf ("%d %d 0\n", k, l);
      NewClause();
      printf ("%d %d 0\n", -k, -l);
      NewClause();
    }
  while (--ands >= 0)
    {
      l = arity[ands];
      assert (l < MAX);
      i = pick (0, nlayers-1);
      lhs = pick (low[i], high[i]);
      mark [lhs] = 1;
      lhs *= SIGN ();
      clause[0] = lhs;
      printf ("%d ", lhs);
      for (k = 1; k <= l; k++)
	{
	  j = pick (0, nlayers-1);
	  rhs = pick (low[j], high[j]);
	  if (mark [rhs]) { k--; continue; }
	  mark [rhs] = 1;
	  rhs *= SIGN ();
	  clause[k] = rhs;
	  printf ("%d ", rhs);
	}
      printf ("0\n");
      NewClause();
      for (k = 1; k <= l; k++)
      {		 
	     printf ("%d %d 0\n", -clause[0], -clause[k]);
	     NewClause();
	  }
      for (k = 0; k <= l; k++)
	mark [abs (clause[k])] = 0;
    }
  map -= m;
  free (map);
  free (mark);
  free (clauses);
  free (arity);
  free (high);
  free (low);
  free (width);
  free (nunused);
  free (quant);
  for (i = 0; i < nlayers; i++)
    free (layers[i]), free (unused[i]);
  free (layers);
  if (clssSoFar != n) 
  {
	  printf("ERROR: clauses-so-far %d must be equal to pre-calculated clauses %d\n", clssSoFar, n);
  }
  printf("s 0");
  return 0;
}
