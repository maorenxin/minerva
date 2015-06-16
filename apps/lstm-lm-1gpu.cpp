#include <minerva.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <math.h>
#include <time.h>

using namespace std;
using namespace minerva;

#define MaxV 10005
#define MaxL 200

map<string, int> wids;
vector<vector<int> > train_sents, test_sents;
vector<int> gpu;

NArray Softmax(NArray m)
{
	NArray maxes = m.Max(0);
	NArray e = Elewise::Exp(m - maxes);
	return e / e.Sum(0);
}

class LSTMModel
{
	public:
		LSTMModel(int vocab_size, int input_size, int hidden_size)
		{
			Layers[0] = input_size;
			Layers[1] = hidden_size;
			Layers[2] = vocab_size;

			ig_weight_data = NArray::Randn({Layers[1], Layers[0]}, 0.0, 0.1);
			fg_weight_data = NArray::Randn({Layers[1], Layers[0]}, 0.0, 0.1);
			og_weight_data = NArray::Randn({Layers[1], Layers[0]}, 0.0, 0.1);
			ff_weight_data = NArray::Randn({Layers[1], Layers[0]}, 0.0, 0.1);

			ig_weight_prev = NArray::Randn({Layers[1], Layers[1]}, 0.0, 0.1);
			fg_weight_prev = NArray::Randn({Layers[1], Layers[1]}, 0.0, 0.1);
			og_weight_prev = NArray::Randn({Layers[1], Layers[1]}, 0.0, 0.1);
			ff_weight_prev = NArray::Randn({Layers[1], Layers[1]}, 0.0, 0.1);

			ig_weight_cell = NArray::Randn({Layers[1], 1}, 0.0, 0.1);
			fg_weight_cell = NArray::Randn({Layers[1], 1}, 0.0, 0.1);
			og_weight_cell = NArray::Randn({Layers[1], 1}, 0.0, 0.1);
			ff_weight_cell = NArray::Randn({Layers[1], 1}, 0.0, 0.1);

			ig_weight_bias = NArray::Zeros({Layers[1], 1});
			fg_weight_bias = NArray::Zeros({Layers[1], 1});
			og_weight_bias = NArray::Zeros({Layers[1], 1});
			ff_weight_bias = NArray::Zeros({Layers[1], 1});

			decoder_weights = NArray::Randn({Layers[2], Layers[1]}, 0.0, 0.1);
			decoder_bias = NArray::Zeros({Layers[2], 1});

			for (int i = 0; i < vocab_size; ++ i)
				emb_weight[i] = NArray::Randn({Layers[0], 1}, 0.0, 0.1);

			printf("model size: [%d %d %d].\n", Layers[0], Layers[1], Layers[2]);
		}

		void train_step(vector<int> sent, int words,
			NArray &weight_update_ig_data, NArray &weight_update_ig_prev, NArray &weight_update_ig_cell, NArray &weight_update_ig_bias,
			NArray &weight_update_fg_data, NArray &weight_update_fg_prev, NArray &weight_update_fg_cell, NArray &weight_update_fg_bias,
			NArray &weight_update_og_data, NArray &weight_update_og_prev, NArray &weight_update_og_cell, NArray &weight_update_og_bias,
			NArray &weight_update_ff_data, NArray &weight_update_ff_prev, NArray &weight_update_ff_bias,
			NArray &dBd, NArray &dWd, vector<NArray> &dEmb, int fake_batch_size = 512)
		{
			int E = Layers[0];
			int N = Layers[1];
			int K = Layers[2];

			NArray data[MaxL], Hout[MaxL];
			Hout[0] = NArray::Zeros({N, fake_batch_size});
			NArray act_ig[MaxL], act_fg[MaxL], act_og[MaxL], act_ff[MaxL];
			NArray C[MaxL], dY[MaxL];
			C[0] = NArray::Zeros({N, fake_batch_size});

			dBd = NArray::Zeros({K, 1});
			dWd = NArray::Zeros({K, N});
			NArray dHout[MaxL];

			float sent_ll = 0;
			int Tau = sent.size();

			for (int t = 1; t < Tau; ++ t)
			{
				//data[t] = emb_weight[sent[t - 1]];
				data[t] = NArray::Zeros({E, fake_batch_size});
				//float *target_ptr = new float[K];
				//memset(target_ptr, 0, K * sizeof(float));
				//target_ptr[sent[t]] = 1;
				//NArray target = NArray::MakeNArray({K, 1}, shared_ptr<float> (target_ptr));
				NArray target = NArray::Zeros({K, fake_batch_size});

				act_ig[t] = ig_weight_data * data[t] + ig_weight_prev * Hout[t - 1] + Elewise::Mult(C[t - 1], ig_weight_cell) + ig_weight_bias;
				act_ig[t] = Elewise::SigmoidForward(act_ig[t]);

				act_fg[t] = fg_weight_data * data[t] + fg_weight_prev * Hout[t - 1] + Elewise::Mult(C[t - 1], fg_weight_cell) + fg_weight_bias;
				act_fg[t] = Elewise::SigmoidForward(act_fg[t]);

				act_ff[t] = ff_weight_data * data[t] + ff_weight_prev * Hout[t - 1] + ff_weight_bias;
				act_ff[t] = Elewise::TanhForward(act_ff[t]);

				C[t] = Elewise::Mult(act_ig[t], act_ff[t]) + Elewise::Mult(C[t - 1], act_fg[t]);

				act_og[t] = og_weight_data * data[t] + og_weight_prev * Hout[t - 1] + Elewise::Mult(C[t], og_weight_cell) + og_weight_bias;
				act_og[t] = Elewise::SigmoidForward(act_og[t]);

				Hout[t] = Elewise::Mult(Elewise::TanhForward(C[t]), act_og[t]);

				NArray Y = Softmax(decoder_weights * Hout[t] + decoder_bias);

				dY[t] = Y - target;
				dBd += dY[t].Sum(1);
				dWd += dY[t] * Hout[t].Trans();

				//auto y_ptr = Y.Get();
				//float *output_ptr = y_ptr.get();
				//float output = output_ptr[sent[t]];
				//if (output < 1e-20) output = 1e-20;
				//sent_ll += log2(output);
			}

			weight_update_ig_data = NArray::Zeros({N, E});
			weight_update_ig_prev = NArray::Zeros({N, N});
			weight_update_ig_cell = NArray::Zeros({N, 1});
			weight_update_ig_bias = NArray::Zeros({N, 1});

			weight_update_fg_data = NArray::Zeros({N, E});
			weight_update_fg_prev = NArray::Zeros({N, N});
			weight_update_fg_cell = NArray::Zeros({N, 1});
			weight_update_fg_bias = NArray::Zeros({N, 1});

			weight_update_og_data = NArray::Zeros({N, E});
			weight_update_og_prev = NArray::Zeros({N, N});
			weight_update_og_cell = NArray::Zeros({N, 1});
			weight_update_og_bias = NArray::Zeros({N, 1});

			weight_update_ff_data = NArray::Zeros({N, E});
			weight_update_ff_prev = NArray::Zeros({N, N});
			weight_update_ff_bias = NArray::Zeros({N, 1});

			NArray last_dHout = NArray::Zeros({N, fake_batch_size});
			dEmb = initw(Tau, {E, fake_batch_size});

			for (int t = Tau - 1; t; -- t)
			{
				dHout[t] = decoder_weights.Trans() * dY[t] + last_dHout;
				NArray tanhC = Elewise::TanhForward(C[t]);
				NArray sen_og = Elewise::Mult(dHout[t], tanhC);
				NArray dC = Elewise::Mult((1 - Elewise::Mult(tanhC, tanhC)), Elewise::Mult(dHout[t], act_og[t]));

				// BP from og
				sen_og = Elewise::Mult(Elewise::Mult(act_og[t], (1.0 - act_og[t])), sen_og);
				dEmb[t] = og_weight_data.Trans() * sen_og;
				dC += og_weight_cell.Trans() * sen_og;

				// BP from fg controled gate
				NArray sen_fg = Elewise::Mult(C[t - 1], dC);

				// BP from ig controled gate
				NArray sen_ig = Elewise::Mult(act_ff[t], dC);
				NArray sen_ff = Elewise::Mult((1 - Elewise::Mult(act_ff[t], act_ff[t])), Elewise::Mult(act_ig[t], dC));
				last_dHout = ff_weight_prev.Trans() * sen_ff;
				dEmb[t] += ff_weight_data.Trans() * sen_ff;

				// BP from fg
				sen_fg = Elewise::Mult(Elewise::Mult(act_fg[t], (1.0 - act_fg[t])), sen_fg);
				dEmb[t] += fg_weight_data.Trans() * sen_fg;

				// BP from ig
				sen_ig = Elewise::Mult(Elewise::Mult(act_ig[t], (1.0 - act_ig[t])), sen_ig);
				dEmb[t] += ig_weight_data.Trans() * sen_ig;

				// derivatives on weight matrix and bias
				weight_update_ig_data += sen_ig * data[t].Trans();
				weight_update_ig_prev += sen_ig * Hout[t - 1].Trans();
				weight_update_ig_cell += Elewise::Mult(sen_ig, C[t - 1]).Sum(1);
				weight_update_ig_bias += sen_ig.Sum(1);

				weight_update_fg_data += sen_fg * data[t].Trans();
				weight_update_fg_prev += sen_fg * Hout[t - 1].Trans();
				weight_update_fg_cell += Elewise::Mult(sen_fg, C[t - 1]).Sum(1);
				weight_update_fg_bias += sen_fg.Sum(1);

				weight_update_og_data += sen_og * data[t].Trans();
				weight_update_og_prev += sen_og * Hout[t - 1].Trans();
				weight_update_og_cell += Elewise::Mult(sen_og, C[t]).Sum(1);
				weight_update_og_bias += sen_og.Sum(1);

				weight_update_ff_data += sen_ff * data[t].Trans();
				weight_update_ff_prev += sen_ff * Hout[t - 1].Trans();
				weight_update_ff_bias += sen_ff.Sum(1);
			}
		}

		vector<NArray> initw(int size, Scale scale)
		{
			vector<NArray> res;
			for (int i = 0; i < size; ++ i)
			{
				NArray Z = NArray::Zeros(scale);
				res.push_back(Z);
			}
			return res;
		}

		void train(vector<vector<int> > sents, int words, int num_epochs, int num_gpu = 1, float learning_rate = 0.1)
		{
			int E = Layers[0];
			int N = Layers[1];
			int K = Layers[2];
			ms.wait_for_all();
			time_t last_time = time(NULL);
			for (int epoch_id = 0; epoch_id < num_epochs; ++ epoch_id)
			{
				NArray weight_update_ig_data = NArray::Zeros({N, E});
				NArray weight_update_ig_prev = NArray::Zeros({N, N});
				NArray weight_update_ig_cell = NArray::Zeros({N, 1});
				NArray weight_update_ig_bias = NArray::Zeros({N, 1});

				NArray weight_update_fg_data = NArray::Zeros({N, E});
				NArray weight_update_fg_prev = NArray::Zeros({N, N});
				NArray weight_update_fg_cell = NArray::Zeros({N, 1});
				NArray weight_update_fg_bias = NArray::Zeros({N, 1});

				NArray weight_update_og_data = NArray::Zeros({N, E});
				NArray weight_update_og_prev = NArray::Zeros({N, N});
				NArray weight_update_og_cell = NArray::Zeros({N, 1});
				NArray weight_update_og_bias = NArray::Zeros({N, 1});

				NArray weight_update_ff_data = NArray::Zeros({N, E});
				NArray weight_update_ff_prev = NArray::Zeros({N, N});
				NArray weight_update_ff_bias = NArray::Zeros({N, 1});

				NArray dBd = NArray::Zeros({K, 1});
				NArray dWd = NArray::Zeros({K, N});
				vector<NArray> dEmb;

				float epoch_ll = 0;
				for (unsigned int s = 0; s < sents.size(); ++ s)
				{
					if (s == 1000) break;

					train_step(sents[s], words,
						weight_update_ig_data, weight_update_ig_prev, weight_update_ig_cell, weight_update_ig_bias,
						weight_update_fg_data, weight_update_fg_prev, weight_update_fg_cell, weight_update_fg_bias,
						weight_update_og_data, weight_update_og_prev, weight_update_og_cell, weight_update_og_bias,
						weight_update_ff_data, weight_update_ff_prev, weight_update_ff_bias,
						dBd, dWd, dEmb);

					//epoch_ll += sent_ll;

					if (s % 2 == 0)
					{
						MinervaSystem& ms = MinervaSystem::Instance();
						ms.wait_for_all();
					}
					continue;

					float rate = learning_rate;
					ig_weight_prev -= rate * weight_update_ig_prev;
					ig_weight_data -= rate * weight_update_ig_data;
					ig_weight_cell -= rate * weight_update_ig_cell;
					ig_weight_bias -= rate * weight_update_ig_bias;

					fg_weight_prev -= rate * weight_update_fg_prev;
					fg_weight_data -= rate * weight_update_fg_data;
					fg_weight_cell -= rate * weight_update_fg_cell;
					fg_weight_bias -= rate * weight_update_fg_bias;

					og_weight_prev -= rate * weight_update_og_prev;
					og_weight_data -= rate * weight_update_og_data;
					og_weight_cell -= rate * weight_update_og_cell;
					og_weight_bias -= rate * weight_update_og_bias;

					ff_weight_prev -= rate * weight_update_ff_prev;
					ff_weight_data -= rate * weight_update_ff_data;
					ff_weight_bias -= rate * weight_update_ff_bias;

					decoder_weights -= rate * dWd;
					decoder_bias -= rate * dBd;

					//for (int t = 1; t < dEmb.size(); ++ t)
					//	emb_weight[sents[s][t - 1]] -= rate * dEmb[t];

				}

				//float epoch_ent = epoch_ll * (-1) / words;
				//float epoch_ppl = exp2(epoch_ent);
				auto current_time = time(NULL);
				printf("Epoch %d\n", epoch_id + 1);
				cout << "time consumed: " << difftime(current_time, last_time) << " seconds" << endl;
				current_time = last_time;
			}
		} // end of void train()

		void test(vector<vector<int> > sents, int words)
		{
			int E = Layers[0];
			int N = Layers[1];
			int K = Layers[2];
			float test_ll = 0;

			for (unsigned int s = 0; s < sents.size(); ++ s)
			{
				NArray data, Hout[MaxL];
				Hout[0] = NArray::Zeros({N, 1});
				NArray act_ig, act_fg, act_og, act_ff;
				NArray C[MaxL], dY[MaxL];
				C[0] = NArray::Zeros({N, 1});

				NArray dBd = NArray::Zeros({K, 1});
				NArray dWd = NArray::Zeros({K, N});
				NArray dHout[MaxL], dEmb[MaxL];

				vector<int> sent = sents[s];
				int Tau = sent.size();
				float sent_ll = 0;
				for (int t = 1; t < Tau; ++ t)
				{
					data = emb_weight[sent[t - 1]];
					float *target_ptr = new float[K];
					memset(target_ptr, 0, K * sizeof(float));
					target_ptr[sent[t]] = 1;
					NArray target = NArray::MakeNArray({K, 1}, shared_ptr<float> (target_ptr));

					act_ig = ig_weight_data * data + ig_weight_prev * Hout[t - 1] + Elewise::Mult(ig_weight_cell, C[t - 1]) + ig_weight_bias;
					act_ig = Elewise::SigmoidForward(act_ig);

					act_fg = fg_weight_data * data + fg_weight_prev * Hout[t - 1] + Elewise::Mult(fg_weight_cell, C[t - 1]) + fg_weight_bias;
					act_fg = Elewise::SigmoidForward(act_fg);

					act_ff = ff_weight_data * data + ff_weight_prev * Hout[t - 1] + ff_weight_bias;
					act_ff = Elewise::TanhForward(act_ff);

					C[t] = Elewise::Mult(act_ig, act_ff) + Elewise::Mult(act_fg, C[t - 1]);

					act_og = og_weight_data * data + og_weight_prev * Hout[t - 1] + Elewise::Mult(og_weight_cell, C[t]) + og_weight_bias;
					act_og = Elewise::SigmoidForward(act_og);

					Hout[t] = Elewise::Mult(act_og, Elewise::TanhForward(C[t]));

					NArray Y = Softmax(decoder_weights * Hout[t] + decoder_bias);

					auto y_ptr = Y.Get();
					float *output_ptr = y_ptr.get();
					float output = output_ptr[sent[t]];
					if (output < 1e-20) output = 1e-20;
					sent_ll += log2(output);
				}
				test_ll += sent_ll;
			}
			auto test_ent = test_ll * (-1) / words;
			auto test_ppl = exp2(test_ent);
			printf("Test PPL = %f\n", test_ppl);
		} // end of void test()

	private:
		int Layers[3];
		NArray ig_weight_data, fg_weight_data, og_weight_data, ff_weight_data;
		NArray ig_weight_prev, fg_weight_prev, og_weight_prev, ff_weight_prev;
		NArray ig_weight_cell, fg_weight_cell, og_weight_cell, ff_weight_cell;
		NArray ig_weight_bias, fg_weight_bias, og_weight_bias, ff_weight_bias;
		NArray decoder_weights, decoder_bias;
		NArray emb_weight[MaxV];

		MinervaSystem& ms = MinervaSystem::Instance();
};

void ReadData(int &train_words, int &test_words)
{
	train_words = test_words = 0;
	wids.clear();
	string bos, eos, unk;
	bos = "-bos-";
	eos = "-eos-";
	unk = "<unk>";
	wids[bos] = 0;
	wids[eos] = 1;
	wids[unk] = 2;

	freopen("/home/cs_user/minerva/owl/apps/train", "r", stdin);
	char s[500];
	int pointer = 2;

	for (; gets(s); )
	{
		int L = strlen(s);
		vector<int> sent;
		sent.push_back(wids[bos]);
		string w = "";
		for (int i = 0; i < L; ++ i)
			if (s[i] == ' ')
			{
				if (wids.find(w) == wids.end())
				{
					wids[w] = pointer;
					sent.push_back(pointer ++);
				}
				else
					sent.push_back(wids[w]);
				w = "";
			}
			else w += s[i];
		if (w.length() > 0)
			if (wids.find(w) == wids.end())
			{
				wids[w] = pointer;
				sent.push_back(pointer ++);
			}
			else
				sent.push_back(wids[w]);
		sent.push_back(wids[eos]);
		train_sents.push_back(sent);
		train_words += sent.size() - 1;
	}

	freopen("/home/cs_user/minerva/owl/apps/test", "r", stdin);
	for (; gets(s); )
	{
		int L = strlen(s);
		vector<int> sent;
		sent.push_back(wids[bos]);
		string w = "";
		for (int i = 0; i < L; ++ i)
			if (s[i] == ' ')
			{
				if (wids.find(w) == wids.end())
					sent.push_back(wids[unk]);
				else
					sent.push_back(wids[w]);
				w = "";
			}
			else w += s[i];
		if (w.length() > 0)
			if (wids.find(w) == wids.end())
				sent.push_back(wids[unk]);
			else
				sent.push_back(wids[w]);
		sent.push_back(wids[eos]);
		test_sents.push_back(sent);
		test_words += sent.size() - 1;
	}

	printf("read %d train samples, %d test samples.\n", train_sents.size(), test_sents.size());
	printf("vocabulary size: %d\n", wids.size());
	printf("%d train words, %d test words in total.\n", train_words, test_words);
}

int H = 10;

int main(int argc, char** argv)
{
	MinervaSystem::Initialize(&argc, &argv);
	MinervaSystem& ms = MinervaSystem::Instance();
	ms.SetDevice(ms.device_manager().CreateGpuDevice(0));
	int train_words, test_words;
	ReadData(train_words, test_words);
	if (argc == 2) H = atoi(argv[1]);
	LSTMModel model(wids.size(), H, H);
	for (int i = 0; i < 10; ++ i)
	{
		model.train(train_sents, train_words, 1, 1);
		model.test(test_sents, test_words);
	}
}