#include "q.h"

joblink_t *head_ejl;

joblink_t *make_jl()
  {
  joblink_t *jl = head_ejl;
  
  if (jl)
    {
    head_ejl = jl->un;
    clear(*jl);
    return jl;
    }
  
  return calloc(sizeof(joblink_t),1);
  }

void free_jl(joblink_t *jl)
  {
  // free data
  if (jl->dir)
    free(jl->dir);
  
  if (jl->parms)
    {
    free(jl->parms[0]);
    free(jl->parms);
    }
  
  jl->un = head_ejl;
  head_ejl = jl;
  }
