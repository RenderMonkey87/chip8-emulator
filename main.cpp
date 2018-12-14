//written by Anton Botvalde

#include <SDL.h>
#include <stdio.h>
#include <string>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <assert.h>
#include <chrono>
#include <map>
#include <thread>

//ignore fopen warning
#ifdef _WIN32
#pragma warning(disable: 4996)
#endif

#ifdef DEBUG_MODE
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define NOT_REACHED() (assert(0))
#else
#define LOG(...)
#define NOT_REACHED()
#endif

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

//display width and height for chip-8 system
constexpr u8 WIDTH = 64;
constexpr u8 HEIGHT = 32;

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;

//60 hz display refresh rate
constexpr u8 FRAMES_PER_SECOND = 60;
//9 cpu instructions per frame
constexpr u16 INSTRUCTIONS_PER_FRAME = 540 / FRAMES_PER_SECOND;

using namespace std::chrono;
using Framerate = duration<steady_clock::rep, std::ratio<1, FRAMES_PER_SECOND>>;

const auto pf = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
//black pixels for off
const auto off = SDL_MapRGBA(pf, 0, 0, 0, 255);
//green pixels for on
const auto on = SDL_MapRGBA(pf, 0, 255, 0, 255);

//SDL uses anonymous enum for scancodes so we map raw values for easy lookup
//TODO - better key mapping, perhaps a text file?
std::map<u16, u8> key_map
{
	{ 0x027, 0 },{ 0x062 , 0 },					//0 and KP_0
	{ 0x01E, 1 },{ 0x059 , 1 },					//1 and KP_1
	{ 0x01F, 2 },{ 0x05A , 2 },{ 0x051 , 2 },	//2 and KP_2 and DOWN
	{ 0x020, 3 },{ 0x05B , 3 },					//3 and KP_3
	{ 0x021, 4 },{ 0x05C , 4 },{ 0x050 , 4 },	//4 and KP_4 and LEFT
	{ 0x022, 5 },{ 0x05D , 5 },					//5 and KP_5
	{ 0x023, 6 },{ 0x05E , 6 },{ 0x04F , 6 },	//6 and KP_6 and RIGHT
	{ 0x024, 7 },{ 0x05F , 7 },					//7 and KP_7
	{ 0x025, 8 },{ 0x060 , 8 },{ 0x052 , 8 },	//8 and KP_8 and UP
	{ 0x026, 9 },{ 0x061 , 9 },					//9 and KP_9
	{ 0x004, 10 },								//A
	{ 0x005, 11 },								//B
	{ 0x006, 12 },								//C
	{ 0x007, 13 },								//D
	{ 0x008, 14 },								//E
	{ 0x009, 15 },								//F
};

//font sprites are 5 bytes
constexpr u8 fonts_offset = 5;
constexpr u8 fonts[] =
{
	//0
	0xF0,0x90,0x90,0x90,0xF0,
	//1
	0x20,0x60,0x20,0x20,0x70,
	//2
	0xF0,0x10,0xF0,0x80,0xF0,
	//3
	0xF0,0x10,0xF0,0x10,0xF0,
	//4
	0x90,0x90,0xF0,0x10,0x10,
	//5
	0xF0,0x80,0xF0,0x10,0xF0,
	//6
	0xF0,0x80,0xF0,0x90,0xF0,
	//7
	0xF0,0x10,0x20,0x40,0x40,
	//8
	0xF0,0x90,0xF0,0x90,0xF0,
	//9
	0xF0,0x90,0xF0,0x10,0xF0,
	//A
	0xF0,0x90,0xF0,0x90,0x90,
	//B
	0xE0,0x90,0xE0,0x90,0xE0,
	//C
	0xF0,0x80,0x80,0x80,0xF0,
	//D
	0xE0,0x90,0x90,0x90,0xE0,
	//E
	0xF0,0x80,0xF0,0x80,0xF0,
	//F
	0xF0,0x80,0xF0,0x80,0x80,
};

struct Chip8
{
	//main memory
	u8 mem[4096] = { };
	//our display buffer, we take the easy way here instead of following the spec
	//precisely which specifies display buffer should be packed in mem[0xF00-0xFFF]
	u8 pixels[WIDTH][HEIGHT] = { };
	//16 8-bit data register
	u8 v_reg[16] = { 0 };
	//60 hz delay timer
	u8 delay_timer = 0;
	//60 hz sound timer
	u8 sound_timer = 0;
	//key buffer, store which key is currently pressed
	u8 keys[16] = { };
	//stack pointer
	u8 sp = 0;
	u16 stack[16] = { };
	//program counter
	u16 pc = 0x200;
	//memory address register
	u16 i_addr_reg = 0;

	u16 program_size = 0;

	u64 cycle_count = 0;

	//https://en.wikipedia.org/wiki/CHIP-8#Opcode_table
	//http://devernay.free.fr/hacks/chip8/C8TECH10.HTM

	//opcodes are 2 bytes, example 0x00E0
	// ==> op1 = 0x00
	// ==> op2 = 0xE0
	void decode_instruction(unsigned char* op_buffer)
	{
		const u8 opcode1 = op_buffer[0];
		const u8 opcode2 = op_buffer[1];

		switch (opcode1 >> 4)
		{
		case 0x00:
			if (opcode1 == 0x00 && opcode2 == 0xE0)
			{
				LOG("disp_clear opcode %02x%02x\n", opcode1, opcode2);
				memset(pixels, 0, sizeof(pixels));
				pc += 2;
			}
			else if (opcode1 == 0x00 && opcode2 == 0xEE)
			{
				LOG("return opcode %02x%02x\n", opcode1, opcode2);
				pc = stack[--sp];
			}
			else
			{
				LOG("INVALID OPCODE %02x%02x\n", opcode1, opcode2);
				NOT_REACHED();
			}
			break;
		case 0x01:
		{
			u16 old_pc = pc;
			LOG("goto opcode %02x%02x\n", opcode1, opcode2);
			pc = ((opcode1 & 0x0F) << 8) | opcode2;

			if (pc == old_pc)
			{
				LOG("inf loop detected!\n");
			}
			break;
		}
		case 0x02:
			LOG("call subroutine opcode %02x%02x\n", opcode1, opcode2);
			stack[sp++] = pc + 2;
			pc = ((opcode1 & 0x0F) << 8) | opcode2;
			break;
		case 0x03:
			LOG("if vx == nn opcode %02x%02x\n", opcode1, opcode2);
			if (v_reg[opcode1 & 0x0F] == opcode2)
			{
				pc += 2;
			}
			pc += 2;
			break;
		case 0x04:
			LOG("if vx != nn opcode %02x%02x\n", opcode1, opcode2);
			if (v_reg[opcode1 & 0x0F] != opcode2)
			{
				pc += 2;
			}
			pc += 2;
			break;
		case 0x05:
			LOG("if vx == vy opcode %02x%02x\n", opcode1, opcode2);
			if (v_reg[opcode1 & 0x0F] == v_reg[opcode2 >> 4])
			{
				pc += 2;
			}
			pc += 2;
			break;
		case 0x06:
			LOG("vx = nn opcode %02x%02x\n", opcode1, opcode2);
			v_reg[opcode1 & 0x0F] = opcode2;
			pc += 2;
			break;
		case 0x07:
			LOG("vx += nn opcode %02x%02x\n", opcode1, opcode2);
			v_reg[opcode1 & 0x0F] += opcode2;
			pc += 2;
			break;
		case 0x08:
		{
			const u8 x = opcode1 & 0x0F;
			const u8 y = opcode2 >> 4;
			switch (opcode2 & 0x0F)
			{
			case 0x00:
				LOG("vx = vy opcode %02x%02x\n", opcode1, opcode2);
				v_reg[x] = v_reg[y];
				pc += 2;
				break;
			case 0x01:
				LOG("vx = vx|vy opcode %02x%02x\n", opcode1, opcode2);
				v_reg[x] |= v_reg[y];
				pc += 2;
				break;
			case 0x02:
				LOG("vx = vx&vy opcode %02x%02x\n", opcode1, opcode2);
				v_reg[x] &= v_reg[y];
				pc += 2;
				break;
			case 0x03:
				LOG("vx = vx^vy opcode %02x%02x\n", opcode1, opcode2);
				v_reg[x] ^= v_reg[y];
				pc += 2;
				break;
			case 0x04:
			{
				LOG("vx += vy opcode %02x%02x\n", opcode1, opcode2);
				u16 res = v_reg[x] + v_reg[y];
				v_reg[0x0F] = res > 0xFF; //set Vf to carry if overflow
				v_reg[x] = res & 0xFF; //just keep lower 8 bits of res
				pc += 2;
				break;
			}
			case 0x05:
			{
				LOG("vx -= vy opcode %02x%02x\n", opcode1, opcode2);
				v_reg[0x0F] = v_reg[x] > v_reg[y];
				v_reg[x] -= v_reg[y];
				pc += 2;
				break;
			}
			case 0x06:
				LOG("vx >>=1 opcode %02x%02x\n", opcode1, opcode2);
				v_reg[0x0F] = v_reg[x] & 0x01;
				v_reg[x] >>= 1;
				pc += 2;
				break;
			case 0x07:
			{
				LOG("vx = vy - vx opcode %02x%02x\n", opcode1, opcode2);
				v_reg[0x0F] = v_reg[y] > v_reg[x];
				v_reg[x] = v_reg[y] - v_reg[x];
				pc += 2;
				break;
			}
			case 0x0E:
				LOG("vx <<=1 opcode %02x%02x\n", opcode1, opcode2);
				v_reg[0x0F] = (v_reg[x] & 0x80) > 1;
				v_reg[x] <<= 1;
				pc += 2;
				break;
			default:
				LOG("INVALID OPCODE %02x%02x\n", opcode1, opcode2);
				NOT_REACHED();
				break;
			}
			break;
		}
		case 0x09:
			LOG("if vx != vy opcode %02x%02x\n", opcode1, opcode2);
			if (v_reg[opcode1 & 0x0F] != v_reg[opcode2 >> 4])
			{
				pc += 2;
			}
			pc += 2;
			break;
		case 0xA:
			LOG("i = nnn opcode %02x%02x\n", opcode1, opcode2);
			i_addr_reg = ((opcode1 & 0x0F) << 8) | opcode2;
			pc += 2;
			break;
		case 0xB:
			LOG("pc = v0 + nnn opcode %02x%02x\n", opcode1, opcode2);
			pc = v_reg[0] + (((opcode1 & 0x0F) << 8) | opcode2);
			pc += 2;
			break;
		case 0xC:
			LOG("vx = rand() & nn opcode %02x%02x\n", opcode1, opcode2);
			v_reg[opcode1 & 0x0F] = (rand() % 256) & opcode2;
			pc += 2;
			break;
		case 0xD:
		{
			LOG("draw(vx, vy, n) opcode %02x%02x\n", opcode1, opcode2);
			const u8 x = v_reg[opcode1 & 0x0F];
			const u8 y = v_reg[opcode2 >> 4];
			const u8 pixel_height = opcode2 & 0x0F;

			v_reg[0x0F] = 0;
			//height
			for (u8 i = 0; i < pixel_height; ++i)
			{
				//byte is packed with 8 pixels bits 
				u8 sprite_bits = mem[i_addr_reg + i];
				//width
				for (u8 j = 0; j < 8; ++j)
				{
					//get current pixel
					const u8 current_pixel = sprite_bits >> 7;
					const u8 old_pixel = pixels[(j + x) % WIDTH][(i + y) % HEIGHT];
					const u8 display_pixel = current_pixel ^ old_pixel;

					pixels[(j + x) % WIDTH][(i + y) % HEIGHT] = display_pixel;

					if (old_pixel == 1 && display_pixel == 0)
					{
						v_reg[0x0F] = 1;
					}

					//shift out our current pixel
					sprite_bits <<= 1;
				}
			}

			pc += 2;
			break;
		}
		case 0xE:
		{
			const u8 x = opcode1 & 0x0F;
			switch (opcode2)
			{
			case 0x9E:
				LOG("if key() == vx opcode %02x%02x\n", opcode1, opcode2);
				if (keys[v_reg[x]] == 1)
				{
					pc += 2;
				}
				pc += 2;
				break;
			case 0xA1:
				LOG("if key() != vx opcode %02x%02x\n", opcode1, opcode2);
				LOG("V%d = %d\n", x, v_reg[x]);
				if (keys[v_reg[x]] == 0)
				{
					pc += 2;
				}
				pc += 2;
				break;
			default:
				LOG("INVALID opcode %02x%02x\n", opcode1, opcode2);
				NOT_REACHED();
				break;
			}
			break;
		}
		case 0xF:
		{
			const u8 x = opcode1 & 0x0F;
			switch (opcode2)
			{
			case 0x07:
				LOG("vx = get_delay() opcode %02x%02x\n", opcode1, opcode2);
				v_reg[x] = delay_timer;
				pc += 2;
				break;
			case 0x0A:
			{
				LOG("vx = get_key() opcode %02x%02x\n", opcode1, opcode2);
				for (u8 i = 0; i < sizeof(keys); ++i)
				{
					if (keys[i] != 0)
					{
						v_reg[x] = 1;
						pc += 2;
						break;
					}
				}
				break;
			}
			case 0x15:
				LOG("delay_timer(vx) opcode %02x%02x\n", opcode1, opcode2);
				delay_timer = v_reg[x];
				pc += 2;
				break;
			case 0x18:
				LOG("sound_timer(vx) opcode %02x%02x\n", opcode1, opcode2);
				sound_timer = v_reg[x];
				pc += 2;
				break;
			case 0x1E:
			{
				LOG("i += vx opcode %02x%02x\n", opcode1, opcode2);
				i_addr_reg += v_reg[x];
				pc += 2;
				break;
			}
			case 0x29:
				LOG("i = sprite_addr[vx] opcode %02x%02x\n", opcode1, opcode2);
				i_addr_reg = v_reg[x] * fonts_offset;
				pc += 2;
				break;
			case 0x33:
			{
				LOG("bcd opcode %02x%02x\n", opcode1, opcode2);
				u8 value = v_reg[x];

				mem[i_addr_reg] = value / 100;
				mem[i_addr_reg + 1] = (value % 100) / 10;
				mem[i_addr_reg + 2] = value % 10;

				pc += 2;
				break;

			}
			case 0x55:
				LOG("reg_dump(vx, &i) opcode %02x%02x\n", opcode1, opcode2);
				for (u8 i = 0; i <= x; ++i)
				{
					mem[i_addr_reg + i] = v_reg[i];
				}
				pc += 2;
				break;
			case 0x65:
				LOG("reg_load(vx, &i) opcode %02x%02x\n", opcode1, opcode2);
				for (u8 i = 0; i <= x; ++i)
				{
					v_reg[i] = mem[i_addr_reg + i];
				}
				pc += 2;
				break;
			default:
				LOG("INVALID OPCODE %02x%02x\n", opcode1, opcode2);
				NOT_REACHED();
				break;
			}
			break;
		}
		}
	}

	bool read_program_to_mem(const char* file_name)
	{
		FILE* file = fopen(file_name, "rb");

		if (!file)
		{
			return false;
		}

		//copy font sprites to mem first
		memcpy(mem, fonts, sizeof(fonts));

		fseek(file, 0, SEEK_END);
		program_size = ftell(file);
		rewind(file);

		pc = 0x200;
		fread(mem + pc, 1, program_size, file);

		fclose(file);
		return true;
	}

	u64 execute_cycle()
	{
		decode_instruction(&mem[pc]);
		return ++cycle_count;
	}

	void update_timers()
	{
		if (delay_timer > 0)
		{
			--delay_timer;
		}
		if (sound_timer > 0)
		{
			--sound_timer;
		}
	}
};

void update_texture(SDL_Texture* texture, SDL_Renderer* renderer, const Chip8& c8)
{
	unsigned char* bytes = nullptr;
	int pitch = 0;

	SDL_LockTexture(texture, nullptr, reinterpret_cast<void**>(&bytes), &pitch);
	for (u8 y = 0; y < HEIGHT; ++y)
	{
		// take account for the pitch since texture probably will be bigger due to padding
		u32* ptr = (u32 *)(bytes + pitch * y);
		for (u8 x = 0; x < WIDTH; ++x)
		{  
			if (c8.pixels[x][y] == 0)
			{
				*ptr = off;
			}
			else
			{
				*ptr = on;
			}
			++ptr;
		}
	}
	SDL_UnlockTexture(texture);
	//scale our texture to match the window dimensions
	SDL_Rect destination = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	SDL_RenderCopy(renderer, texture, NULL, &destination);
	SDL_RenderPresent(renderer);
}

int main(int argc, char* argv[])
{
	Chip8 c8;
	srand(time(NULL));

	if (argc != 2)
	{
		fprintf(stderr, "usage : %s <chip8-program>\n", argv[0]);
		return -1;
	}

	if (!c8.read_program_to_mem(argv[1]))
	{
		fprintf(stderr, "could not open %s\n", argv[1]);
		return -1;
	}

	SDL_Init(SDL_INIT_EVERYTHING);

	SDL_Window* main_window = SDL_CreateWindow("chip-8 emulator",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		SDL_WINDOW_SHOWN
	);

	SDL_Renderer* renderer = SDL_CreateRenderer(main_window, -1, 0);
	SDL_RenderClear(renderer);

	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

	bool quit = false;
	SDL_Event event;

	auto next_frame = steady_clock::now() + Framerate{ 1 };
	while (!quit)
	{
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_KEYDOWN)
			{
				const auto& it = key_map.find(event.key.keysym.scancode);

				if (it != key_map.cend())
				{
					c8.keys[it->second] = 1;
				}
			}

			//TODO - clearing the key buffer instantly means that we might
			//risk ever processing the key.. unsure what the expected real behavior is
			else if (event.type == SDL_KEYUP)
			{
				const auto& it = key_map.find(event.key.keysym.scancode);

				if (it != key_map.cend())
				{
					c8.keys[it->second] = 0;
				}
			}

			quit = event.type == SDL_QUIT;
		}

		//run enough instructions before rendering to display
		if (c8.execute_cycle() % INSTRUCTIONS_PER_FRAME != 0)
		{
			continue;
		}

		std::this_thread::sleep_until(next_frame);
		
		update_texture(texture, renderer, c8);
		c8.update_timers();

		next_frame += Framerate{ 1 };
	}

	//Clean up
	SDL_DestroyTexture(texture);
	SDL_DestroyWindow(main_window);
	SDL_Quit();

	return 0;
}