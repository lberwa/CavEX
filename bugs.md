# Bugs/Todos:

* Texture orientation for blocks that have a specific "direction"
  - ~~Bed placement isn't correct yet~~
  - door placement isn't correct yet                                                                                             ⭕

* ~~Random crashes, once in a while.~~ Maybe I'll implement an optional auto-save, to prevent some headaches and tears

* ~~Particles already spark fire before the torch is showing, after placing a torch~~

* ~~If you jump into water or climb down the ladder, you take damage~~

* ~~if you place a block and there is already a stair block or a torch, the block despawn and there is the new block~~
  - ~~torch~~
  - ~~stair~~
  - ~~rose~~
  - ~~flower~~

* ~~Redstone doesn't really work:~~
  - ~~Redstone Dust~~
  - ~~Redstone Torch~~
  - ~~Redstone Repeater~~
  - ~~Lever~~
  - ~~Stone Button~~
  - ~~Wooden/Stone Pressure Plate~~
  - ~~Detector Rail~~
  - ~~Iron Door~~
  - ~~Wooden Door~~
  - ~~Trapdoor~~
  - ~~Piston~~
  - ~~Sticky Piston~~
  - ~~Dispenser~~
  - ~~Note Block~~
  - ~~TNT~~
  - ~~Powered Rail~~

* ~~If you jump into a block, you will get stuck and won't be able to move~~

* ~~Placing blocks on dirt doesn't consume the block.~~

* ~~Stair and fence hitboxes are incorrect.~~

* ~~Furnace output items despawn when moving an item.~~

* ~~if you destroy one part of the block, the other doesn't despawn~~
  - ~~bed~~
  - ~~door~~

* ~~Blocks don't rotate when placed from a different side:~~
  - ~~Chests~~
  - ~~Trapdoor~~
  - ~~Log~~
  - ~~stairs (top/bottom)~~

* ~~Chests can't combine into a double chest.~~

* ~~sort worlds with date~~

* ~~TNT and Creeper drop glass~~

* ~~still water looks like stone brick~~

* ~~Input system of the PC must work like on the Wii: checking ... every frame all buttons and register them for input_pressed() and input_released()~~

* ~~Upgrader~~

* ~~backup upgrader~~

* ~~frozen config.json if no config.json~~

* ~~save player data of player 2~~

* ~~Buttons like in settings in main menu~~

* ~~visibility bug like vine~~
  ~~(--> gfx_write_buffers(true, true, true) and MODE_OFF)~~
  - ~~Waterlilies~~
  - ~~glass_pane~~
  - ~~pumpkin stem~~
  - ~~melon stem~~
  - ~~vine~~
  - ~~crops~~

* ~~save settings in config.json~~

* ~~sprint with double click~~

* sheep and pigs can't rotate

* ~~sheep and pigs drop nothing~~

* ~~player 2 input prints~~

* ~~screen game settings input is broken~~

* ~~entities are able to going throw chunks and falling down~~

* implement a minecraft terminal                                                                                                     ⭕

* ~~the moon and sun are too little~~

* ~~Implement a fallback boundary at Y = -50 to stop players falling under the world.~~

* ~~in creative inventory add:~~
  - ~~search function~~
  - ~~save function~~
  - ~~move to other screen with IB_INVENTORY_LEFT/RIGHT~~
  - ~~remove slot~~
  - ~~other icons~~

* ~~the menu of creative inventory of player 1 is the same like of player 2~~

* ~~compass and clock can't tell anything in the nether~~

* ~~the player doesn't move his arms if he places a block~~

* ~~you should be able to destroy blocks with destroy number -1 if you are in creative mode~~

* ~~IB_ANY doesn't ask all buttons~~

* ~~shadow bugs on the wii~~

* ~~gpu on the pc is too slowly so it runs with 20-55fps on fullscreen~~

* ~~world generator needs too much cpu and is too slowly~~

* ~~you don't see the cracks on the blocks on the wii~~

* if player 2 goes into the nether portal he can't go out of the loading screen while player 1 doesn't go to the nether portal too ?? ⭕

* if you go throw the portal, and you wait 5 sekonds you go again to the world ??                                                     ⭕

* ~~the texture of the creative menu is broken on the wii~~

* may load ogg files insead of pcm/wav                                                                                                ⭕

* ~~config.json in ~/.cavex~~

* ~~Window Quit (x) don't work in the server menu~~

* ~~bg in the menu -> real VR~~

* ~~loading chunks is broken on the Wii because the RAM problem-fixes~~

* ~~add a better food logic~~

* ~~the mp3 loader expect 10 mp3 files~~

* ~~the position of the cursor in the main menu is broken because the 2 player mode~~

* ~~compatibility with Mac OS and Windows~~ (just tested with wine; don't test it on Mac OS)

* ~~the head of the men in the inventory is behind the body on PC~~

* ~~the inventory item of chests isn't correct~~

* ~~you can't stack doors~~

* ~~there is no item in the creative inventory:~~
  - ~~fence~~
  - ~~door~~
  - ~~iron door~~
  - ~~vine~~
  - ~~waterlily~~
  - ~~glas pane~~
  - ~~crops~~

* ~~there is superfluous item in the creative inventory:~~
  - ~~boat motor~~

* entities can't swim                                                                                                                   ⭕

* ~~there is sound missing/broken:~~
  - ~~under water you hear footsteps~~
  - ~~in the water you doesn't hear something~~
  - ~~on the leadder~~
  - ~~if you pic some item~~
  - ~~if entities walks~~
  - ~~on snow~~
  - ~~sand~~
  - ~~glass~~
  - ~~door~~
  - ~~trapdoor~~
  - ~~piston~~
  - ~~pressure plate~~

* bad mob AI                                                                                                                             ⭕

* ~~tailgrass looks like dead bush~~

* ~~items doesn't despawn, if there is no block, next to him:~~
  - ~~tailgrass~~
  - ~~rail~~
  - ~~detector rail~~
  - ~~powered rail~~
  - ~~redstone torch~~
  - ~~redstone torch lit~~
  - ~~redstone~~
  - ~~pressure plate~~
  - ~~sampling~~
  - ~~flower~~
  - ~~dead bush~~
  - ~~rose~~
  - ~~brown mushroom~~
  - ~~red mushroom~~
  - ~~torch~~
  - ~~snow~~
  - ~~door~~

* ~~scroll with the mouse wheel~~

* ~~bg sound on the wii is quiet if you play an other sound~~

* ~~2d problem:            [./docs/bug1.png](./docs/bug1.png) [./docs/bug2.png](./docs/bug2.png)~~









* diff metadata:
  - torch
  - chest
  - stairs
  - sign

* not in b1.7.3:
  - end-portal
  - end-stone
  - iron_chest
  - 2d-tree
  - record_13
  - block_jukebox vs block_record_cat ?
  - spawn eggs

* save:
  - chests
  - furnace
  - sign
  - entities:
    * Mobs
    * Items
    * ...

* ids:
  - item motor
  - lapis lazui -- damage number
  - kacao       -- damage number

* item metadata:
  - diamand = uint8=255 !!
  - stone and stole
  - bogen
  - fishing_hook = uint8=255 !!
  - armor
  - collor/dye


