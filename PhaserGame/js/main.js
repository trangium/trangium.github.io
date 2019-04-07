function Hero(game,x,y) {
    Phaser.Sprite.call(this,game,x,y,"hero");
    this.anchor.set(0.5,0.5); 
    this.game.physics.enable(this);
    this.body.collideWorldBounds = true;
}

Hero.prototype = Object.create(Phaser.Sprite.prototype);
Hero.prototype.constructor = Hero;

Hero.prototype.move = function (direction) {
    const SPEED = 400;
    this.body.velocity.x = direction * SPEED;
}

Hero.prototype.jump = function () {
    const JUMP_SPEED = 750;
    let canJump = this.body.touching.down;

    if (canJump) {
        this.body.velocity.y = -JUMP_SPEED;
    }

}

PlayState = {};

PlayState.preload = function () {
    this.game.load.image("background","images/background.png");
    this.game.load.json("level:1","data/level01.json");
    this.game.load.image("ground","images/ground.png");
    this.game.load.image("grass:8x1","images/grass_8x1.png");
    this.game.load.image("grass:6x1","images/grass_6x1.png");
    this.game.load.image("grass:4x1","images/grass_4x1.png");
    this.game.load.image("grass:2x1","images/grass_2x1.png");
    this.game.load.image("grass:1x1","images/grass_1x1.png");
    this.game.load.image("hero","images/hero_stopped.png");
    this.game.load.audio("sfx:jump","audio/jump.wav");
}

PlayState.init = function () {
    this.game.renderer.renderSession.roundPixels = true;
    this.keys = this.game.input.keyboard.addKeys({
        left: Phaser.KeyCode.LEFT,
        right: Phaser.KeyCode.RIGHT,
        up: Phaser.KeyCode.UP
    });
    this.keys.up.onDown.add(function () {
        let didJump = this.hero.jump();
        if (didJump) {
            this.sfx.jump.play();
        }
    },this);
}

PlayState._handleInput = function() {
    if (this.keys.left.isDown) {
        this.hero.move(-1);
    } else if (this.keys.right.isDown) {
        this.hero.move(1);
    } else {
        this.hero.move(0);
    }
}

PlayState._handleCollisions = function() {
    this.game.physics.arcade.collide(this.hero, this.platforms);
}
PlayState.update = function () {
    this._handleCollisions();
    this._handleInput();
}

window.onload = function () {
    let game = new Phaser.Game(960,600,Phaser.AUTO,"game");
    game.state.add("play",PlayState);
    game.state.start("play");
}

PlayState.create = function () {
    this.sfx = {
        jump: this.game.add.audio("sfx:jump")
    }

    this.game.add.image(0,0,"background");
    this._loadlevel(this.game.cache.getJSON("level:1"));
}

PlayState._spawnPlatform = function (platform) {
    let sprite = this.platforms.create(platform.x,platform.y,platform.image);
    this.game.physics.enable(sprite);
    sprite.body.allowGravity = false;
    sprite.body.immovable = true;
}


PlayState._spawnCharacters =  function (data) {
    this.hero = new Hero(this.game,data.hero.x,data.hero.y);
    this.game.add.existing(this.hero);
}

PlayState._loadlevel = function(data) {

    this.platforms = this.game.add.group();

    data.platforms.forEach(this._spawnPlatform,this);
    this._spawnCharacters({hero: data.hero});
    const GRAVITY = 1800;
    this.game.physics.arcade.gravity.y = GRAVITY;
}
