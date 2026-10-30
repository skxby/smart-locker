/* Exercise the actual assembly mailboxes with concurrent producers. Unused
 * hardware/task entry sections are discarded, never replaced with fake success. */
#include "../../../firmware/tasks/assembly.c"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static atomic_uint off_count;
uint8_t fixture_uid[12];
void fixture_critical(int enter)
{ if(enter) assert(!pthread_mutex_lock(&mutex)); else assert(!pthread_mutex_unlock(&mutex)); }
void mcu_board_force_off(void) { atomic_fetch_add(&off_count,1u); }
static void *producer(void *arg)
{
    unsigned i; uintptr_t id=(uintptr_t)arg;
    for(i=1;i<=2000u;i++) {
        control_message_t m={0}; locker_status_t s;
        m.token=(uint32_t)(id*10000u+i); m.kind=1;
        do { s=post(&m); } while(s==LOCKER_STATUS_BUSY);
        assert(s==LOCKER_STATUS_QUEUED);
    }
    return NULL;
}
static void concurrent(void)
{
    pthread_t a,b; unsigned n=0; bool seen[2][2001]={{false}};
    ih=ic=0; assert(!pthread_create(&a,NULL,producer,(void *)1)); assert(!pthread_create(&b,NULL,producer,(void *)2));
    while(n<4000u) {
        control_message_t m; bool have;
        taskENTER_CRITICAL(); have=ic!=0; if(have) { m=inbox[ih]; ih=(ih+1u)%16u; --ic; } taskEXIT_CRITICAL();
        if(have) { unsigned p=m.token/10000u-1u,j=m.token%10000u; assert(p<2 && j>0 && j<=2000 && !seen[p][j]); seen[p][j]=true; ++n; }
    }
    assert(!pthread_join(a,NULL)); assert(!pthread_join(b,NULL));
}
int main(void)
{
    mcu_request_context_t r={0}; locker_wire_request_union_t q={0}; uint32_t gen;
    ready_flag=true; r.message_type=1; r.request_no=1; q.hello.new_session_id.bytes[0]=1;
    assert(assembly_dispatch(NULL,&r,&q)==LOCKER_STATUS_QUEUED); gen=session_generation;
    assert(assembly_dispatch(NULL,&r,&q)==LOCKER_STATUS_QUEUED); assert(session_generation==gen);
    q.hello.new_session_id.bytes[0]=2; assert(assembly_dispatch(NULL,&r,&q)==LOCKER_STATUS_QUEUED);
    assert(session_generation!=gen && inbox[ih].value.request.generation==gen);
    ih=ic=0; concurrent();
    assert(failed && atomic_load(&off_count)>0);
    puts("Actual assembly mailbox concurrent delivery and session generation PASS"); return 0;
}
