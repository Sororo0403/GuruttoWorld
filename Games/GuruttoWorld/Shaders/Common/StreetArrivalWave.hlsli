// シーンの既存スキャン半径と同じ進行を使い、風と街の反応を同期します。
float StreetWaveRadius(float time)
{
    float radius = 0;
    radius += saturate((time - 0.38) / 0.10) * 5;
    radius += saturate((time - 0.48) / 0.09) * 9;
    radius += saturate((time - 0.57) / 0.11) * 24;
    radius += saturate((time - 0.68) / 0.14) * 52;
    radius += saturate((time - 0.82) / 0.13) * 90;
    return radius;
}

float StreetWaveArrival(float distance)
{
    // FXCの戻り値解析で未初期化と判定されないよう、初期値を持つ一つの値へ集約します。
    float arrival = 0.82 + (distance - 90) * 0.13 / 90;
    if (distance <= 5)
        arrival = 0.38 + distance * 0.10 / 5;
    else if (distance <= 14)
        arrival = 0.48 + (distance - 5) * 0.09 / 9;
    else if (distance <= 38)
        arrival = 0.57 + (distance - 14) * 0.11 / 24;
    else if (distance <= 90)
        arrival = 0.68 + (distance - 38) * 0.14 / 52;
    return arrival;
}
