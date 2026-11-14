static BYTE bench_scene[0x600],bench_part[0x118],bench_nodes[PS_MAX_LEVELS+1][0x40];
static uintptr_t bench_vector[2];
static void prepare_bench(PSProfile *p){
 memset(bench_scene,0,sizeof(bench_scene));memset(bench_part,0,sizeof(bench_part));memset(bench_nodes,0,sizeof(bench_nodes));
 original_vt=0x11111111;bench_vector[0]=(uintptr_t)bench_part;bench_vector[1]=0;
 put(bench_scene,0,base+0x6a9dae0);bench_scene[0xa9]=bench_scene[0xaa]=3;
 put(bench_scene,0x5d0,(uintptr_t)bench_vector);put(bench_scene,0x5d8,(uintptr_t)(bench_vector+2));
 put(bench_part,0,(uintptr_t)(shadow+1));put32(bench_part,0x20,p->selector);bench_part[0x24]=1;
 put(bench_part,0x60,(uintptr_t)bench_scene);put(bench_part,0xe8,(uintptr_t)bench_nodes);put(bench_part,0x100,(uintptr_t)mock_level);
 unsigned count=p->native_count+p->route_count;
 for(unsigned i=0;i<=count;i++){
  put(bench_nodes[i],0,(uintptr_t)bench_nodes[(i+1)%(count+1)]);put(bench_nodes[i],8,(uintptr_t)bench_nodes[(i+count)%(count+1)]);
  if(i)put32(bench_nodes[i],0x30,i<=p->native_count?p->native_ids[i-1]:p->route[i-1-p->native_count].visual);
 }
 BYTE input[404]={0},output[404];put32(input,4+4*order[2],p->route[0].body==PS_ANY?555:p->route[0].body);
 put32(input,4+4*order[34],p->route[0].bumper==PS_ANY?666:p->route[0].bumper);put32(input,4+4*order[p->selector],p->route[0].input);
 if(PSRouteProfile(p,input,order,output)!=1)abort();memcpy(bench_scene+0x224,output,404);put32(mock_level,0x20,p->route[0].visual);
 original_getter=getter;getter_value=p->selector;enabled=1;record_count=1;
 records[0]=(Registration){.lease=REG_LIVE,.part=(uintptr_t)bench_part,.profile=p,.scene=(uintptr_t)bench_scene};
 expected_return=(uintptr_t)&ps_test_return;initial_return=(uintptr_t)&ps_test_initial_return;draw_return=(uintptr_t)&ps_test_draw_return;cache_return=(uintptr_t)&ps_test_cache_return;
 load_refresh_enabled=1;scene_setup=mock_setup;mock_profile=p;mock_part=bench_part;mock_fail=0;setup_count=0;
}
