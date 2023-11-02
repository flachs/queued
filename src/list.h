#define add_link_to_head(hl,jl,h) do   \
  {                                    \
  jl->h = hl;                          \
  jl->h##n = (hl)->head;               \
  if ((hl)->head) (hl)->head->h##p = jl; \
  (hl)->head = jl;                       \
  if (! (hl)->tail) (hl)->tail = jl;     \
  } while (0)

#define add_link_to_tail(ul,jl,u) do   \
  {                                    \
  jl->u = ul;                          \
  jl->u##p = (ul)->tail;               \
  if ((ul)->tail) (ul)->tail->u##n = jl;   \
  (ul)->tail = jl;                         \
  if (! (ul)->head) (ul)->head=jl;         \
  } while (0)
    
#define remove_link(jl,h) do \
  {                               \
  if (jl->h##p) jl->h##p->h##n = jl->h##n; \
  else jl->h->head = jl->h##n;              \
  if (jl->h##n) jl->h##n->h##p = jl->h##p; \
  else jl->h->tail = jl->h##p;             \
  } while (0)

#define add_link_before(point,jl,u) do     \
  {                                        \
  jl->u##n = point;                        \
  jl->u##p = point->u##p;                  \
  if (point->u##p) point->u##p->u##n = jl; \
  else u##l->head = jl;                    \
  point->u##p = jl;                        \
  } while (0)


