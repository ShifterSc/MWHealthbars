#include "healthbars.h"
#include "primrender.h"

#undef max

#include <algorithm>
#include <string>

#define DEREF(ptr, offset) ( (void*)*(int*)((int)(ptr) + (offset)) )
#define GETVARPTR(type, ptr, offset) ( (type*)(void*)((int)(ptr) + (offset)) )
#define GETVAR(type, ptr, offset) *(GETVARPTR(type, ptr, offset))

#define HALF_PI 1.5708f

const int** copCarTable = (const int**)0x92ce9c;
const int* numCopCars = (const int*)0x92cea4;
const float* deltaTime = (const float*)0x9259bc;
const float* simTime = (const float*)0x9885d8;

// CONFIG
const bool hideOnPause = false;
const float barHeight = 0.13f;
const float barWidth = 1.4f;
const float height = 1.15f;
const float colorAlpha = 0.7f;
const D3DXCOLOR green = D3DXCOLOR(0, 1, 0, colorAlpha);
const D3DXCOLOR lightGreen = D3DXCOLOR(0.75, 1, 0.375, colorAlpha);
const D3DXCOLOR yellow = D3DXCOLOR(1, 1, 0, colorAlpha);
const D3DXCOLOR orange = D3DXCOLOR(1, 0.5, 0, colorAlpha);
const D3DXCOLOR red = D3DXCOLOR(1, 0, 0, colorAlpha);
const D3DXCOLOR healthAnimationColor = D3DXCOLOR(0.9, 0.9, 0.9, colorAlpha);
const D3DXCOLOR borderColor = D3DXCOLOR(1, 1, 1, colorAlpha);
const D3DXCOLOR borderColorDestroyed = D3DXCOLOR(1.0, 0.5, 0.5, colorAlpha);
const float nearFadeOutMin = 2.f;
const float nearFadeOutMax = 5.f;
const float farFadeOutMin = 70.f;
const float farFadeOutMax = 110.f;

float map(float x, float in_min, float in_max, float out_min, float out_max) {
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float saturate(float x) {
	if (x < 0)
		return 0;
	else if (x > 1)
		return 1;
	return x;
}

void HealthbarRenderer::UpdateCopCars() {
	int numCops = *numCopCars;
	for (int i = 0; i < numCops; i++) {
		void* copCarPtr = (void*)(*copCarTable)[i];

		VehicleClass vehicleClass = GETVAR(VehicleClass, copCarPtr, 0x6c);
		if (vehicleClass == VehicleClass::CHOPPER)
			continue;

		CopCarInfo* copCar = &copCars[copCarPtr];
		copCar->health = 0;

		void* destrVehiclePtr = DEREF(copCarPtr, 0x4c);
		if (destrVehiclePtr) {
			copCar->health = 1 - GETVAR(float, destrVehiclePtr, 0x3c);
			if (copCar->health >= copCar->healthAnimation) {
				copCar->healthAnimation = copCar->health;
				copCar->healthReduceCountdown = 0.5;
			}
		}

		void* vehicle = DEREF((int)copCarPtr - 0x80, 0x4c);
		void* vehicleB = DEREF(vehicle, 0x30);
		void* vehicleC = DEREF(vehicleB, 0x0);
		copCar->position = GETVAR(D3DXVECTOR3, vehicleC, 0x10);

		int carState = GETVAR(int, copCarPtr, 0x84);
		if (carState == 0) {
			if (copCar->disabledTimer == 0.f)
				copCar->disabledTimer = 0.0001f;
		}
		else {
			copCar->disabledTimer = 0;
		}
	}

	// Purge step
	for (auto It = copCars.begin(); It != copCars.end(); It++) {
		for (int i = 0; i < numCops; i++) {
			if ((int)It->first == (int)(*copCarTable)[i]) {
				break;
			}
			else if (i == numCops - 1) {
				copCars.erase(It);
				return;
			}
		}
	}

}

void HealthbarRenderer::Draw()
{
	UpdateState();//更新信息

	//---------- 排除不显示血条的情况 --------------
	//CopFreezeCam 不显示血条
	if (pursuitActive && gameMomentCamEnabled && pursuitActiveCounter < 1.f && timeScale < 1.f) {
		inCopFreezeCam = true;
	}
	else if (!isPaused) {
		inCopFreezeCam = false;
	}	

	if (isPaused && hideOnPause || !pursuitActive || inCopFreezeCam) {
		return;
	}
	// -----------------------------------------------------

	UpdateCopCars();//更新信息

	healthBars.clear();

	//遍历所有警车 确定血条的所有参数
	for (auto& copCar : copCars) {
		float alpha = 1;

		CopCarInfo* car = &copCar.second;

		//如果似透了 直接下一辆
		if (car->health <= 0 && car->deathTimer >= 1.5f) {
			continue;
		}

		// 刚刚似的 等它死透了再删血条
		if (car->disabledTimer > 0.f) {
			car->disabledTimer += simDeltaTime;
			if (car->disabledTimer > 2.f)
				continue;
		}


		HealthBarDraw healthBar;

		float scale = 1;

		//指定边框颜色
		healthBar.borderColor = borderColor;

		// 车辆已瘫痪
		if (car->health <= 0) {
			healthBar.borderColor = borderColorDestroyed;
			if (car->deathTimer == 0) {
				// died on this frame
				//瘫痪时设置的生命值减少动画速度
				car->deathReduceSpeed = std::max(car->healthAnimation, 0.75f);
				car->healthReduceCountdown = 0;//设置动画延迟为0
			}
			// start fading out 1sec after death
			//透明度逐渐增大 血条淡出
			if (car->deathTimer > 1)
				alpha *= 1.f - (car->deathTimer - 1.f) * 2.f;
			car->deathTimer += simDeltaTime;
		}
		else { //未瘫痪则强制重置计时器
			car->deathTimer = 0;
		}

		//当车辆受到伤害后，控制代表“旧生命值”的白色动画条 (healthAnimation)，使其在短暂延迟后，平滑地缩减到与当前实际生命值 (health) 相同的位置
		if (car->health < car->healthAnimation) {
			//延迟逻辑：每次收到伤害后 延迟计时器会被设置为一个正数，然后随时间递减归零
			if (car->healthReduceCountdown > 0) {
				car->healthReduceCountdown -= simDeltaTime;
			}
			else {
				if (car->health > 0) {
					//逐渐减小边框长度 直到和血条长度相同
					car->healthAnimation -= 0.75 * simDeltaTime;
					if (car->health > car->healthAnimation)
						car->healthAnimation = car->health;
				}	
				else {
					//若直接瘫痪 则执行瘫痪时的动画逻辑
					car->healthAnimation -= car->deathReduceSpeed * simDeltaTime;
				}
			}
		}
		//车辆瘫痪 则血条闪烁，同时设置计时器执行淡出步骤
		if (car->health <= 0) {
			// Flash when dead
			float x = car->deathTimer / 0.05f + HALF_PI;
			alpha *= sinf(x) / 2.f + 0.5f;
		}

		D3DXVECTOR3 barWorldPos = D3DXVECTOR3(car->position[2], -car->position[0], car->position[1] + height);//确定血条的3D世界坐标
		D3DXVec3Transform(&healthBar.drawPosViewSpace, &barWorldPos, viewMat); //将3D坐标转换为摄像机坐标

		//确保非0
		float health = std::max(0.f, car->health);
		float healthAnimation = std::max(0.f, car->healthAnimation);

		// 确定血条颜色
		if (health > 0.8)
			healthBar.healthBarColor = green;
		else if (health > 0.6)
			healthBar.healthBarColor = lightGreen;
		else if (health > 0.4)
			healthBar.healthBarColor = yellow;
		else if (health > 0.2)
			healthBar.healthBarColor = orange;
		else
			healthBar.healthBarColor = red;

		//血量条根据其与玩家摄像机的距离远近来改变透明度，从而实现近处淡入和远处淡出的平滑视觉效果
		float depth = healthBar.drawPosViewSpace.z;
		alpha *= saturate(map(depth, nearFadeOutMin, nearFadeOutMax, 0, 1));
		alpha *= saturate(map(depth, farFadeOutMax, farFadeOutMin, 0, 1));

		if (alpha <= 0)
			continue;

		healthBar.healthBarColor.a *= alpha;
		healthBar.borderColor.a *= alpha;

		healthBar.healthAnimationColor = healthAnimationColor;
		healthBar.healthAnimationColor.a *= alpha;

		healthBar.health = health;
		healthBar.healthAnimation = healthAnimation;

		//加入数组
		healthBars.emplace_back(healthBar);
	}


	// -------------------------- 核心绘制模块 ---------------------------------
	// Sort back-to-front for correct transparency order
	std::sort(healthBars.begin(), healthBars.end());

	//初始化 准备渲染环境
	primitiveRenderer.Begin(&projMat);

	float barHalfHeight = barHeight / 2.f;
	float barHalfWidth = barWidth / 2.f;

	//循环并绘制每一个血量条
	for (auto& healthBar : healthBars) {
		D3DXVECTOR4* pos = &healthBar.drawPosViewSpace;

		D3DXVECTOR4 screen[8] = {
			//血条边框的四个角
			D3DXVECTOR4(pos->x - barHalfWidth, pos->y - barHalfHeight, pos->z, 1),
			D3DXVECTOR4(pos->x + barHalfWidth, pos->y - barHalfHeight, pos->z, 1),
			D3DXVECTOR4(pos->x + barHalfWidth, pos->y + barHalfHeight, pos->z, 1),
			D3DXVECTOR4(pos->x - barHalfWidth, pos->y + barHalfHeight, pos->z, 1),

			//真实血量的位置（通过右侧顶点确定）
			D3DXVECTOR4(pos->x + (-barHalfWidth + barWidth * healthBar.health), pos->y + barHalfHeight, pos->z, 1),
			D3DXVECTOR4(pos->x + (-barHalfWidth + barWidth * healthBar.health), pos->y - barHalfHeight, pos->z, 1),

			//血条边框的位置
			D3DXVECTOR4(pos->x + (-barHalfWidth + barWidth * healthBar.healthAnimation), pos->y + barHalfHeight, pos->z, 1),
			D3DXVECTOR4(pos->x + (-barHalfWidth + barWidth * healthBar.healthAnimation), pos->y - barHalfHeight, pos->z, 1),
		};

		//绘制血条
		primitiveRenderer.DrawRect(healthBar.healthBarColor, screen[0], screen[5], screen[4], screen[3]);
		
		//如果血条有变化 则绘制动画条
		if (healthBar.health < healthBar.healthAnimation) {
			primitiveRenderer.DrawRect(healthBar.healthAnimationColor,screen[5], screen[7], screen[6], screen[4]);
		}

		//绘制边框
		for (int i = 0; i < 4; i++) {
			primitiveRenderer.DrawLine(healthBar.borderColor,8, screen[0], screen[1], screen[1], screen[2], screen[2], screen[3], screen[3], screen[0]);
		}
	}

	//恢复渲染环境
	primitiveRenderer.End();
}

void HealthbarRenderer::UpdateState()
{
	void* symSystem = (void*)*(int*)0x9885e0;
	int state = 0;
	if (symSystem) {
		state = GETVAR(int, symSystem, 0x2c);
		timeScale = GETVAR(float, symSystem, 0x24);
	}
	isPaused = state != 3;

	if (lastSimTime == -1)
		lastSimTime = *simTime;

	simDeltaTime = *simTime - lastSimTime;
	lastSimTime = *simTime;

	viewMat = (D3DXMATRIX*)0x9842D0;
	viewProjMat = (D3DXMATRIX*)0x984350;	

	// I don't know where to find the projection matrix, so I'll calculate it myself (projMat = inverse(viewMat) * viewProjMat)	
	D3DXMatrixInverse(&projMat, NULL, viewMat);
	D3DXMatrixMultiply(&projMat, &projMat, viewProjMat);

	pursuitActive = false;
	void* soundAi = DEREF(0x993cc8, 0);
	if (soundAi) {
		void* pursuitAi = DEREF(soundAi, 0x130);
		pursuitActive = pursuitAi != nullptr;
	}

	if (pursuitActive) {
		pursuitActiveCounter += *deltaTime;
	}
	else {
		pursuitActiveCounter = 0;
		copCars.clear();
	}

	// Figure out if "Game moment camera" is enabled in the options to determine when to start showing healthbars on pursuit start
	void* jumpCamA = DEREF(0x91cf90, 0);
	if (jumpCamA) {
		void* jumpCamB = DEREF(jumpCamA, 0x10);
		if (jumpCamB) {
			gameMomentCamEnabled = GETVAR(bool, jumpCamB, 0x47);
		}
	}
}
