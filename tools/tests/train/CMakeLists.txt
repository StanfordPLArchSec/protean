
set(wrap_funcs malloc calloc realloc reallocarray free)

function(add_train_test NAME SOURCE)
  add_executable(${NAME} ${SOURCE})
  target_link_libraries(${NAME} PRIVATE TrainRuntime)
  foreach(wrap_func IN LISTS wrap_funcs)
    target_link_options(${NAME} PRIVATE -Wl,--wrap=${wrap_func})
  endforeach()
endfunction()


add_train_test(train_malloc malloc.c)
add_train_test(train_calloc calloc.c)
add_train_test(train_realloc realloc.c)
add_train_test(train_reallocarray reallocarray.c)
add_train_test(train_strdup strdup.c)
