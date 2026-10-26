#ifndef OCEAN_WAVES_HLSLI
#define OCEAN_WAVES_HLSLI
// 深水域の線形重力波を使います: omega^2 = g k（NVIDIA GPU Gems 1、第 1 章）。
// 単位はメートル・秒で、解析的な微分を使います。写真やスクリーン空間反射は使いません。
// 9 方向・振幅の重み・ピクセルの範囲は描画表現のための設定であり、
// 実測の気象値ではありません。この有界な高さ場は砕ける波を表現しません。
struct SeaHit { float distance; float3 position; float3 normal; bool hit; };

float SeaFootprint(float3 origin, float3 ray)
{
    float planeDistance=abs((origin.y-cameraForward.w)/max(abs(ray.y),1e-5));
    // 水平な海面に投影した 1 ピクセルの角度範囲を近似します。交点を求める間はこの範囲を
    // 固定し、微分を解析的に計算できるようにします。
    float angularPixel=2.0*abs(cameraUp.w)/max(viewportSize.y,1.0);
    return min(4000.0,planeDistance*angularPixel/max(abs(ray.y),.025));
}
float3 SeaShape(float2 p,float footprint)
{
    float3 shape=0;
    [loop] for(int i=0;i<(int)intersection.x;++i)
    {
        float j=(float)i;
        float period=max(.2,waterPeriod.w)*pow(max(waveShape.z,0.001),j);
        float omega=6.283185307/period;
        float k=omega*omega/9.80665;
        float angle=waveShape.x+j*waveShape.y;
        float2 direction=float2(cos(angle),sin(angle));
        float amplitude=max(0,zenithAmplitude.w)*pow(max(waveShape.w,0.001),j)*exp(-k*footprint);
        float s,c; sincos(k*dot(direction,p)-omega*eyeTime.w+j*cloudShape.w,s,c);
        shape.x+=amplitude*s;
        shape.yz+=amplitude*k*c*direction;
    }
    return shape;
}
float2 SeaBounds(float footprint)
{
    float2 bounds=0;
    [loop] for(int i=0;i<(int)intersection.x;++i)
    {
        float j=(float)i;
        float omega=6.283185307/(max(.2,waterPeriod.w)*pow(max(waveShape.z,0.001),j));
        float k=omega*omega/9.80665;
        float a=max(0,zenithAmplitude.w)*pow(max(waveShape.w,0.001),j)*exp(-k*footprint);
        bounds+=float2(a,a*k);
    }
    return bounds; // 高さの絶対値と水平方向の勾配の上限。
}
SeaHit IntersectSea(float3 origin,float3 direction,float maxDistance)
{
    SeaHit result;
    result.distance=0; result.position=origin; result.normal=float3(0,1,0); result.hit=false;
    if(maxDistance>0 && abs(direction.y)>=1e-7)
    {
        float footprint=SeaFootprint(origin,direction);
        float2 bounds=SeaBounds(footprint);
        if(bounds.x<1e-6)
        {
            float planeT=(cameraForward.w-origin.y)/direction.y;
            if(planeT>1e-4 && planeT<=maxDistance)
            { result.hit=true; result.distance=planeT; result.position=origin+direction*planeT; }
            // 振幅 0 は、厳密に海面の平面と一致します。
        }
        else
        {
            float a=(cameraForward.w+bounds.x-origin.y)/direction.y;
            float b=(cameraForward.w-bounds.x-origin.y)/direction.y;
            float t=max(0,min(a,b)),limit=min(maxDistance,max(a,b));
            if(limit>t)
            {
                // 残差と Lipschitz 定数に基づく保守的な前進量で、手前の波頭を飛び越えないようにします。
                float lipschitz=abs(direction.y)+bounds.y*length(direction.xz);
                float initial=origin.y-cameraForward.w-SeaShape(origin.xz,footprint).x;
                float side=initial>=0 ? 1.0 : -1.0;
                bool marchFinished=false;
                // 水面下から始まる場合は、上向きの法線を持つ出口の界面を探します。
                // SeaRadiance は水面上だけを対象とし、水中の吸収は扱いません。
                [loop] for(int iteration=0;iteration<(int)intersection.y;++iteration)
                {
                    float3 position=origin+direction*t;
                    float3 shape=SeaShape(position.xz,footprint);
                    float residual=position.y-cameraForward.w-shape.x;
                    float tolerance=max(.0008,t*.000003);
                    if(abs(residual)<=tolerance && t>1e-4)
                    {
                        result.hit=true; result.distance=t; result.position=position;
                        result.normal=normalize(float3(-shape.y,1,-shape.z));
                        marchFinished=true;
                        break;
                    }
                    if(residual*side<0) {marchFinished=true;break;}
                    t+=max(abs(residual)/max(lipschitz,1e-6),.0001);
                    if(t>limit) {marchFinished=true;break;}
                }
                if(!marchFinished)
                {
                    // ほぼ接線方向のレイは収束が遅いため、Newton 法のステップを無制限にせず、
                    // 符号が変わる区間内で解を確定します。
                    float3 lowPosition=origin+direction*t;
                    float low=(lowPosition.y-cameraForward.w-SeaShape(lowPosition.xz,footprint).x)*side;
                    float3 highPosition=origin+direction*limit;
                    float high=(highPosition.y-cameraForward.w-SeaShape(highPosition.xz,footprint).x)*side;
                    if(low>=0 && high<=0)
                    {
                        float lo=t,hi=limit;
                        [loop] for(int refine=0;refine<(int)intersection.z;++refine)
                        {
                            t=lerp(lo,hi,clamp(low/max(low-high,1e-8),.15,.85));
                            float3 position=origin+direction*t;
                            float3 shape=SeaShape(position.xz,footprint);
                            float residual=(position.y-cameraForward.w-shape.x)*side;
                            result.distance=t; result.position=position;
                            result.normal=normalize(float3(-shape.y,1,-shape.z));
                            result.hit=true;
                            if(abs(residual)<=max(.0008,t*.000003)) break;
                            if(residual>0) {lo=t;low=residual;} else {hi=t;high=residual;}
                        }
                    }
                }
            }
        }
        // 水面上から下向きに進むレイは、高さの上下限で挟まれた領域を必ず通過します。
        // 解像できない波の細部に限り、接線方向の数値解法が失敗した場合は
        // 平均平面で解決します。領域全体がクリップ距離内にあることを要求し、
        // この近似によってクリップ済みの界面が再び現れないようにします。
        if(!result.hit && direction.y<0 && origin.y>cameraForward.w+bounds.x &&
            bounds.x<=footprint*.5)
        {
            float planeT=(cameraForward.w-origin.y)/direction.y;
            float farSlabT=(cameraForward.w-bounds.x-origin.y)/direction.y;
            if(planeT>1e-4 && farSlabT<=maxDistance)
            {
                result.hit=true; result.distance=planeT;
                result.position=origin+direction*planeT;
                result.normal=float3(0,1,0);
            }
        }
    }
    return result;
}
float WaterFresnel(float cosTheta)
{
    float c=saturate(cosTheta),eta=max(1.0001,weather.z);
    float ct=sqrt(max(0,1-(1-c*c)/(eta*eta)));
    float rs=(c-eta*ct)/max(c+eta*ct,1e-7);
    float rp=(eta*c-ct)/max(eta*c+ct,1e-7);
    return saturate(.5*(rs*rs+rp*rp)); // 無偏光の誘電体の Fresnel 反射（PBRT 第 4 版）。
}
float3 SeaRadiance(SeaHit hit,float3 incomingRay)
{
    float3 reflection=reflect(incomingRay,hit.normal);
    float cosine=saturate(dot(-incomingRay,hit.normal));
    float fresnel=WaterFresnel(cosine);
    // 反射方向が地平線の下を向く場合も、実際の反射方向を保持します。
    // EnvironmentRadiance は有限の大きさの太陽円盤を含むため、太陽を二重に加算しないでください。
    float3 reflected=EnvironmentRadiance(hit.position+hit.normal*.01,reflection);
    // RGB の内部アルベドは編集可能な表面下散乱の近似です。
    // 空と雲の影を受けた日光で照らし、透過エネルギーで重み付けします。
    float3 diffuseSky=SkyRadiance(hit.position,float3(0,1,0),sunlight.w);
    float3 illumination=diffuseSky+SolarAtPoint(hit.position)*
        CloudSunTransmittance(hit.position)*max(dot(hit.normal,solar.xyz),0)/3.141592654;
    float3 body=max(waterPeriod.rgb,0)*illumination;
    float3 surface=fresnel*reflected+(1-fresnel)*body;
    return surface*AirTransmittance(eyeTime.xyz,incomingRay,hit.distance)+
        SkyRadiance(eyeTime.xyz,incomingRay,hit.distance);
}
#endif
