import bindings from 'bindings';
const cpp_back = bindings('newquote-cpp-backend');
import fs from 'fs';
import fetch from 'node-fetch';
import * as readline from 'node:readline/promises';
import {stdin as input, stdout as output} from 'node:process';

const rl = readline.createInterface({ input, output });

const name = await rl.question("Name: ");
const ava_link = (await rl.question("Avatar link: ")) || "https://vk.com/images/camera_200.png";
const text = await rl.question("Text:\n");
const ava_buf = await (await fetch(ava_link)).arrayBuffer();

fs.writeFileSync('test.png', Buffer.from(cpp_back.gen_quote(text, name, ava_buf)));
console.log("Done, see test.png");
rl.close();
