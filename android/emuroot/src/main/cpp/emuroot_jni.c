/* JNI glue for io.github.mkalmousli.emuroot.Emuroot: the Java API over the C
 * embedding API (include/emuroot.h). No logic lives here. */
#include <emuroot.h>

#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define JNI(ret, name) JNIEXPORT ret JNICALL Java_io_github_mkalmousli_emuroot_Emuroot_##name

/* A Java String[] as a malloc'd, NULL-terminated argv; free with free_strv(). */
static char **to_strv(JNIEnv *env, jobjectArray array)
{
    if (!array) return NULL;
    jsize n = (*env)->GetArrayLength(env, array);
    char **v = calloc((size_t)n + 1, sizeof *v);
    if (!v) return NULL;
    for (jsize i = 0; i < n; i++) {
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, array, i);
        const char *utf = s ? (*env)->GetStringUTFChars(env, s, NULL) : NULL;
        v[i] = strdup(utf ? utf : "");
        if (utf) (*env)->ReleaseStringUTFChars(env, s, utf);
        if (s) (*env)->DeleteLocalRef(env, s);
    }
    return v;
}

static void free_strv(char **v)
{
    if (!v) return;
    for (char **p = v; *p; p++) free(*p);
    free(v);
}

/* Runs fn(ctx-less) with a Java string as UTF-8. */
#define WITH_UTF(env, jstr, var, body) do { \
        const char *var = (*(env))->GetStringUTFChars((env), (jstr), NULL); \
        if (var) { body; (*(env))->ReleaseStringUTFChars((env), (jstr), var); } \
    } while (0)

JNI(jlong, nativeCreate)(JNIEnv *env, jclass cls) { (void)env; (void)cls; return (jlong)(intptr_t)emuroot_create(); }
JNI(void, nativeDestroy)(JNIEnv *env, jclass cls, jlong ctx) { (void)env; (void)cls; emuroot_destroy((emuroot_ctx_t *)(intptr_t)ctx); }

JNI(jint, nativeSetRoot)(JNIEnv *env, jclass cls, jlong ctx, jstring path)
{
    (void)cls;
    jint rc = EMUROOT_ERR_INVAL;
    WITH_UTF(env, path, p, rc = emuroot_set_root((emuroot_ctx_t *)(intptr_t)ctx, p));
    return rc;
}

JNI(jint, nativeBind)(JNIEnv *env, jclass cls, jlong ctx, jstring host, jstring guest)
{
    (void)cls;
    jint rc = EMUROOT_ERR_INVAL;
    WITH_UTF(env, host, h, WITH_UTF(env, guest, g, rc = emuroot_bind((emuroot_ctx_t *)(intptr_t)ctx, h, g)));
    return rc;
}

JNI(jint, nativeSetFakeIds)(JNIEnv *env, jclass cls, jlong ctx, jint uid, jint gid)
{
    (void)env; (void)cls;
    return emuroot_set_fake_ids((emuroot_ctx_t *)(intptr_t)ctx, (unsigned)uid, (unsigned)gid);
}

JNI(jint, nativeSetFakeUname)(JNIEnv *env, jclass cls, jlong ctx, jstring sys, jstring rel, jstring machine)
{
    (void)cls;
    jint rc = EMUROOT_ERR_INVAL;
    WITH_UTF(env, sys, s, WITH_UTF(env, rel, r, WITH_UTF(env, machine, m,
             rc = emuroot_set_fake_uname((emuroot_ctx_t *)(intptr_t)ctx, s, r, m))));
    return rc;
}

JNI(jint, nativeSetIdsFile)(JNIEnv *env, jclass cls, jlong ctx, jstring path)
{
    (void)cls;
    jint rc = EMUROOT_ERR_INVAL;
    WITH_UTF(env, path, p, rc = emuroot_set_ids_file((emuroot_ctx_t *)(intptr_t)ctx, p));
    return rc;
}

JNI(jint, nativeSetVerbose)(JNIEnv *env, jclass cls, jlong ctx, jboolean on)
{
    (void)env; (void)cls;
    return emuroot_set_verbose((emuroot_ctx_t *)(intptr_t)ctx, on ? 1 : 0);
}

/* Starts the guest under a supervisor process with pipes for its stdio.
 * Returns {pid, stdin_write_fd, stdout_read_fd, stderr_read_fd} or null (errno in the exception). */
JNI(jintArray, nativeStart)(JNIEnv *env, jclass cls, jlong ctx, jobjectArray argv_j, jobjectArray envp_j)
{
    (void)cls;
    int in[2] = { -1, -1 }, out[2] = { -1, -1 }, err[2] = { -1, -1 };
    char **argv = to_strv(env, argv_j), **envp = to_strv(env, envp_j);
    jintArray result = NULL;
    if (!argv || !argv[0] || pipe2(in, O_CLOEXEC) || pipe2(out, O_CLOEXEC) || pipe2(err, O_CLOEXEC)) goto done;

    emuroot_spawn_opts_t opts = { in[0], out[1], err[1], 1 };
    pid_t pid = emuroot_spawn((emuroot_ctx_t *)(intptr_t)ctx, argv, envp, &opts);
    if (pid < 0) goto done;

    close(in[0]); close(out[1]); close(err[1]);                  /* the supervisor's ends */
    jint values[4] = { pid, in[1], out[0], err[0] };
    result = (*env)->NewIntArray(env, 4);
    if (result) (*env)->SetIntArrayRegion(env, result, 0, 4, values);
    in[1] = out[0] = err[0] = -1;                                /* now owned by the caller */
    in[0] = out[1] = err[1] = -1;

done:
    for (int i = 0; i < 2; i++) {
        if (in[i] >= 0) close(in[i]);
        if (out[i] >= 0) close(out[i]);
        if (err[i] >= 0) close(err[i]);
    }
    free_strv(argv);
    free_strv(envp);
    return result;
}

JNI(jint, nativeWait)(JNIEnv *env, jclass cls, jint pid) { (void)env; (void)cls; return emuroot_wait((pid_t)pid); }
JNI(jint, nativeTerminate)(JNIEnv *env, jclass cls, jint pid) { (void)env; (void)cls; return emuroot_terminate((pid_t)pid); }

JNI(jstring, nativeDiagnose)(JNIEnv *env, jclass cls)
{
    (void)cls;
    char report[2048];
    emuroot_diagnose(report, sizeof report);
    return (*env)->NewStringUTF(env, report);
}

JNI(jstring, nativeVersion)(JNIEnv *env, jclass cls)
{
    (void)cls;
    char v[64];
    emuroot_version_string(v, sizeof v);
    return (*env)->NewStringUTF(env, v);
}
