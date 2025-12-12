# possible optimize

- simple NRVO: (finished)
  - identify the first local that have same type as the ret, and place it in ret instead of alloc(speculatively assume it will be returned). 
  - if it is returned, that is just ret
  - but if it is not returned, we evaluate the return expr and overwrite it(safe since evaluate is atomic and the lifetime of the local have ended when the ret happens)

- place copy: (finished)
  - if a assignment is between two places that can be proven to not overlap, we can use memcpy instead of load and store
  - if it is proven to be identical, we can do it as no-op
  - else, we fall back to load and store