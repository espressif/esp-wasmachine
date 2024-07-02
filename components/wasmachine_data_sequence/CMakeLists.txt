set(srcs  "src/data_seq.c")
set(include_dir "include")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dir})