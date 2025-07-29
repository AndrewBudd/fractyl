#include "fractyl-diff.h"
#include <stdio.h>
#include <string.h>

// Output callback for emitting diff lines
static int diff_out_line(void *priv, mmbuffer_t *mb, int nbuf) {
    (void)priv; // unused
    
    for (int i = 0; i < nbuf; i++) {
        // Print the line as-is - xdiff already formats it with +/- prefixes
        fwrite(mb[i].ptr, 1, mb[i].size, stdout);
    }
    return 0;
}

// Hunk header callback
static int diff_out_hunk(void *priv, long old_begin, long old_nr,
                        long new_begin, long new_nr,
                        const char *func, long funclen) {
    (void)priv; // unused
    (void)func; // unused for now
    (void)funclen; // unused for now
    
    printf("@@ -%ld,%ld +%ld,%ld @@\n", old_begin, old_nr, new_begin, new_nr);
    return 0;
}

int fractyl_diff_unified(const char *path_a, const char *data_a, size_t size_a,
                        const char *path_b, const char *data_b, size_t size_b,
                        int context_lines) {
    mmfile_t file_a, file_b;
    xpparam_t xpp;
    xdemitconf_t xecfg;
    xdemitcb_t ecb;
    
    // Setup file structures
    file_a.ptr = (char *)data_a;
    file_a.size = size_a;
    file_b.ptr = (char *)data_b;
    file_b.size = size_b;
    
    // Setup parameters
    memset(&xpp, 0, sizeof(xpp));
    memset(&xecfg, 0, sizeof(xecfg));
    memset(&ecb, 0, sizeof(ecb));
    
    // Configure diff parameters
    xpp.flags = 0; // Default flags
    xecfg.ctxlen = context_lines; // Context lines
    
    // Setup callbacks
    ecb.out_hunk = diff_out_hunk;
    ecb.out_line = diff_out_line;
    ecb.priv = NULL;
    
    // Print git-style diff header
    printf("diff --git a/%s b/%s\n", path_a, path_b);
    
    // Handle new/deleted files
    if (!data_a && data_b) {
        printf("new file mode 100644\n");
        printf("index 0000000..0000000\n");
        printf("--- /dev/null\n");
        printf("+++ b/%s\n", path_b);
    } else if (data_a && !data_b) {
        printf("deleted file mode 100644\n");
        printf("index 0000000..0000000\n");
        printf("--- a/%s\n", path_a);
        printf("+++ /dev/null\n");
    } else if (data_a && data_b) {
        printf("index 0000000..0000000 100644\n");
        printf("--- a/%s\n", path_a);
        printf("+++ b/%s\n", path_b);
    }
    
    // Perform the diff
    int result = xdl_diff(&file_a, &file_b, &xpp, &xecfg, &ecb);
    
    return result;
}